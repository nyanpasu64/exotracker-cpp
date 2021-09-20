#define pattern_editor_INTERNAL public
#include "pattern_editor.h"

#include "gui/lib/format.h"
#include "gui/lib/painter_ext.h"
#include "gui/cursor.h"
#include "gui/move_cursor.h"
#include "gui_common.h"
#include "doc/gui_traits.h"
#include "chip_kinds.h"
#include "edit/edit_pattern.h"
#include "util/distance.h"
#include "util/enumerate.h"
#include "util/math.h"
#include "util/release_assert.h"
#include "util/reverse.h"
#include "util/unwrap.h"

#include <fmt/core.h>
#include <gsl/span>
#include <verdigris/wobjectimpl.h>
#include <qkeycode/qkeycode.h>

#include <QColor>
// #include <QDebug>
#include <QFont>
#include <QFontMetrics>
#include <QGradient>
#include <QKeyEvent>
#include <QKeySequence>
// #include <QMessageBox>
#include <QPainter>
#include <QPoint>
#include <QRect>

#include <algorithm>  // std::max, std::clamp
#include <cmath>  // round
#include <functional>  // std::invoke
#include <optional>
#include <stdexcept>
#include <tuple>
#include <variant>
#include <vector>

//#define PATTERN_EDITOR_DEBUG

#ifdef PATTERN_EDITOR_DEBUG
    #define DEBUG_PRINT(...)  fmt::print(stderr, __VA_ARGS__)
#else
    #define DEBUG_PRINT(...)
#endif

namespace gui::pattern_editor {

using gui::lib::color::lerp;
using gui::lib::color::lerp_colors;

using gui::lib::format::format_hex_1;
using gui::lib::format::format_hex_2;
namespace format = gui::lib::format;

using namespace gui::lib::painter_ext;

using util::math::increment_mod;
using util::math::decrement_mod;
using util::reverse::reverse;

using timing::MaybeSequencerTime;
using doc::gui_traits::get_volume_digits;
using doc::gui_traits::is_noise;
using doc::gui_traits::channel_name;
using chip_common::ChipIndex;
using chip_common::ChannelIndex;

PatternEditorShortcuts::PatternEditorShortcuts(QWidget * widget) :
    #define COMMA ,

    #define X(PAIR) \
        PAIR{QShortcut{widget}, QShortcut{widget}}
    SHORTCUT_PAIRS(X, COMMA)
    #undef X
    #undef COMMA

    #define X(KEY)  ,KEY{widget}
    SHORTCUTS(X, )
    #undef X
{}

W_OBJECT_IMPL(PatternEditor)

/*
TODO:
- Recompute font metrics when fonts change (set_font()?) or screen DPI changes.
- QPainter::setPen(QColor) sets the pen width to 1 pixel.
  If we add custom pen width support (based on font metrics/DPI/user config),
  this overload must be banned.
- On high DPI, font metrics automatically scale,
  but dimensions measured in pixels (like header height) don't.
- Should we remove _image and draw directly to the widget?
*/

namespace columns {
    constexpr int EXTRA_WIDTH_DIVISOR = 5;

    // TODO switch to 3-digit ruler/space in decimal mode?
    // If I label fractional beats, this needs to increase to 3 or more.
    constexpr int RULER_WIDTH_CHARS = 2;
}

namespace header {
    constexpr int HEIGHT = 40;

    constexpr int TEXT_X = 8;
    constexpr int TEXT_Y = 20;
}

// # Constructor
static void setup_shortcuts(PatternEditor & self) {
    using config::KeyInt;
    using config::chord;

    config::PatternKeys const& shortcut_keys = get_app().options().pattern_keys;

    auto init_shortcut = [&] (QShortcut & shortcut, QKeySequence const & key) {
        shortcut.setContext(Qt::WidgetShortcut);
        shortcut.setKey(key);
    };

    auto init_pair = [&] (ShortcutPair & pair, KeyInt key) {
        auto shift_key = chord(Qt::SHIFT, key);

        init_shortcut(pair.key, key);
        init_shortcut(pair.shift_key, shift_key);
    };

    #define X(KEY) \
        init_pair(self._shortcuts.KEY, shortcut_keys.KEY);
    SHORTCUT_PAIRS(X, )
    #undef X

    #define X(KEY)  init_shortcut(self._shortcuts.KEY, shortcut_keys.KEY);
    SHORTCUTS(X, )
    #undef X

    // Cursor movement actions clear or extend the selection
    // based on whether Shift is held.
    // But to avoid duplicating the movement action handlers
    // or making them responsible for selections,
    // on_cursor_move() clears/extends the selection
    // before calling the movement action handlers,
    // in the same transaction as moving the cursor.
    using TxMethod = void (PatternEditor::*)(StateTransaction & tx);

    // Regular actions handle clearing the selection (if necessary) themselves,
    // so connect_shortcut()'s callback doesn't create and pass in a transaction
    // to clear/enable the selection.
    using Method = void (PatternEditor::*)();

    enum class AlterSelection {
        Clear,
        Extend,
    };

    static auto const on_cursor_move = [] (
        PatternEditor & self, TxMethod method, AlterSelection alter_selection
    ) {
        auto tx = self._win.edit_unwrap();
        if (alter_selection == AlterSelection::Clear) {
            tx.cursor_mut().clear_select();
        }
        if (alter_selection == AlterSelection::Extend) {
            // Begin or extend selection at old cursor position.
            tx.cursor_mut().enable_select(self._zoom_level);
        }
        // Move cursor.
        std::invoke(method, self, tx);
    };

    // Connect cursor-movement keys to cursor-movement functions
    // (with/without shift held).
    auto connect_shortcut_pair = [&] (ShortcutPair & pair, TxMethod method) {
        // Connect arrow keys to "clear selection and move cursor".
        QObject::connect(
            &pair.key,
            &QShortcut::activated,
            &self,
            [&self, method] () {
                on_cursor_move(self, method, AlterSelection::Clear);
            }
        );

        // Connect shift+arrow to "enable selection and move cursor".
        QObject::connect(
            &pair.shift_key,
            &QShortcut::activated,
            &self,
            [&self, method] () {
                on_cursor_move(self, method, AlterSelection::Extend);
            }
        );
    };

    // Copy, don't borrow, local lambdas.
    #define X(KEY) \
        connect_shortcut_pair(self._shortcuts.KEY, &PatternEditor::KEY##_pressed);
    SHORTCUT_PAIRS(X, )
    #undef X

    auto connect_shortcut = [&] (QShortcut & shortcut, Method method) {
        QObject::connect(
            &shortcut,
            &QShortcut::activated,
            &self,
            [&self, method] () { std::invoke(method, self); }
        );
    };

    #define X(KEY) \
        connect_shortcut(self._shortcuts.KEY, &PatternEditor::KEY##_pressed);
    SHORTCUTS(X, )
    #undef X
}

static PatternFontMetrics calc_single_font_metrics(QFont const & font) {
    auto & visual = get_app().options().visual;
    QFontMetrics metrics{font};

    // height() == ascent() + descent().
    // lineSpacing() == height() + (leading() often is 0).
    // In FamiTracker, all pattern text is uppercase,
    // so GridRect{metrics.boundingRect('Q')} is sufficient.
    // Here, we use ascent()/descent() to support lowercase characters in theory.

    // averageCharWidth() doesn't work well.
    // In the case of Verdana, it's too narrow to even fit numbers.
    constexpr auto width_char = QChar{'M'};
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    int width = metrics.horizontalAdvance(width_char);
#else
    int width = metrics.width(width_char);
#endif

    width = width + visual.font_tweaks.width_adjust;

    // Round up to multiple of 2.
    // This ensures that cell centers (used to draw text) are integers.
    // On Windows, drawing text centered at fractional coordinates can lead to
    // characters being off-center by up to a full pixel on each side.
    // This is probably because QPainter draws text using GDI or similar,
    // and GDI doesn't perform subpixel text positioning.
    width = (width + 1) & ~1;

    // Only width used so far. Instead of ascent/descent, we look at _pixels_per_row.
    return PatternFontMetrics{
        .width=width,
        .ascent=metrics.ascent(),
        .descent=metrics.descent()
    };
}

static void calc_font_metrics(PatternEditor & self) {
    auto & visual = get_app().options().visual;

    self._pattern_font_metrics = calc_single_font_metrics(visual.pattern_font);

    self._pixels_per_row = std::max(
        visual.font_tweaks.pixels_above_text
            + self._pattern_font_metrics.ascent
            + self._pattern_font_metrics.descent
            + visual.font_tweaks.pixels_below_text,
        1
    );
}

static void create_image(PatternEditor & self) {
    /*
    https://www.qt.io/blog/2009/12/16/qt-graphics-and-performance-an-overview

    QImage is designed and optimized for I/O,
    and for direct pixel access and manipulation,
    while QPixmap is designed and optimized for showing images on screen.

    I've measured ARGB32_Premultiplied onto RGB32 to be about 2-4x faster
    than drawing an ARGB32 non-premultiplied depending on the usecase.

    By default a QPixmap is treated as opaque.
    When doing QPixmap::fill(Qt::transparent),
    it will be made into a pixmap with alpha channel which is slower to draw.

    Before moving onto something else, I'll just give a small warning
    on the functions setAlphaChannel and setMask
    and the innocently looking alphaChannel() and mask().
    These functions are part of the Qt 3 legacy
    that we didn't quite manage to clean up when moving to Qt 4.
    In the past the alpha channel of a pixmap, or its mask,
    was stored separately from the pixmap data.
    */

    QPixmap pixmap{QSize{1, 1}};
    // On Windows, it's QImage::Format_RGB32.
    auto format = pixmap.toImage().format();
    self._image = QImage(self.geometry().size(), format);
    self._temp_image = QImage(self.geometry().size(), format);
}

PatternEditor::PatternEditor(MainWindow * win, QWidget * parent)
    : QWidget(parent)
    , _win{*win}
    , _get_document(GetDocument::empty())
    , _shortcuts{this}
{
    // Focus widget on click.
    setFocusPolicy(Qt::ClickFocus);

    setMinimumSize(128, 320);

    calc_font_metrics(*this);
    setup_shortcuts(*this);
    create_image(*this);

    // setAttribute(Qt::WA_Hover);  (generates paint events when mouse cursor enters/exits)
    // setContextMenuPolicy(Qt::CustomContextMenu);
}

doc::Document const& PatternEditor::get_document() const {
    return _get_document();
}

void PatternEditor::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);

    create_image(*this);
    // Qt automatically calls paintEvent().
}

// # Column layout
// See doc.h for documentation of how patterns work.

struct ChannelDraw {
    ChipIndex chip;
    ChannelIndex channel;
    int xleft;
    int xright;
};

namespace SubColumn_ = edit::edit_pattern::SubColumn_;
using edit::edit_pattern::SubColumn;

// # Visual layout.

using cursor::Cursor;
using cursor::CursorX;
using cursor::ColumnIndex;
using cursor::SubColumnIndex;
using cursor::CellIndex;
using main_window::Selection;
using main_window::RawSelection;
using DigitIndex = uint32_t;
using util::distance;

struct RulerOrHandlePx {
    int _left_px;
    int _right_px;

    qreal _center_px;

    [[nodiscard]] int left_px() const {
        return _left_px;
    }
    [[nodiscard]] int right_px() const {
        return _right_px;
    }
    [[nodiscard]] qreal center_px() const {
        return _center_px;
    }
};

/// Maximum number of cells in a subcolumn.
/// Effects have up to 2 characters and 2 digits.
constexpr CellIndex SUBCOL_MAX_CELLS = 4;

/// One column used for selections. May have multiple cursor columns.
struct SubColumnPx {
    SubColumn type;

    /// Number of items the cursor can move into. Must be nonzero.
    CellIndex ncell;

    /// Subcolumn boundaries used for background/selection drawing and click handling.
    int _bounds_left;
    int _bounds_right;

    /// Number of padding pixels from either side of cells to subcolumn boundary.
    /// May not equal cell_left_px[0] - _bounds_left,
    /// because that includes the left DIVIDER_WIDTH and _pad_width does not.
    int _pad_width;

    /// Boundaries of each cell, used for cursor drawing.
    /// Because there is added padding between subcolumns,
    /// there is a gap between _bounds_left and cell_left_px[0],
    /// and between cell_left_px[ncell] and _bounds_right.
    ///
    /// Valid range: [0..ncell] inclusive.
    std::array<int, SUBCOL_MAX_CELLS + 1> cell_left_px;

    /// Center of each cell, used to draw 1 or more characters.
    ///
    /// Valid range: [0..ncell).
    std::array<qreal, SUBCOL_MAX_CELLS> cell_center_px;

// impl
    SubColumnPx(SubColumn type) : type{type} {}

    /// Returns the left boundary of the subcolumn (background).
    /// It's slightly wider than the space used to draw text.
    [[nodiscard]] inline int left_px() const {
        return _bounds_left;
    }

    /// Returns the right boundary of the subcolumn (background).
    /// It's slightly wider than the space used to draw text.
    [[nodiscard]] inline int right_px() const {
        return _bounds_right;
    }

    /// Returns the pixel to draw a cell's text.
    [[nodiscard]] inline qreal center_px() const {
        assert(ncell == 1);
        return cell_center_px[0];
    }

    [[nodiscard]] inline gsl::span<qreal const> cell_centers() const {
        assert(ncell <= SUBCOL_MAX_CELLS);
        return {cell_center_px.data(), ncell};
    }

    /// Returns the horizontal boundaries of a cell, used for drawing the cursor.
    [[nodiscard]] std::tuple<int, int> cell_left_right(CellIndex cell) {
        release_assert(cell < ncell);
        return {
            cell_left_px[cell] - _pad_width, cell_left_px[cell + 1] + _pad_width
        };
    }
};

using SubColumnLayout = std::vector<SubColumnPx>;

struct LeftOfScreen{};
struct RightOfScreen{};

struct ColumnPx {
    ChipIndex chip;
    ChannelIndex channel;
    int left_px;
    int right_px;
    RulerOrHandlePx block_handle;
    SubColumnLayout subcolumns;  // all endpoints lie within [left_px, left_px + width]
};

struct MaybeColumnPx {
    std::variant<LeftOfScreen, ColumnPx, RightOfScreen> value;

    // not explicit
    MaybeColumnPx(LeftOfScreen v) : value{v} {}
    MaybeColumnPx(ColumnPx v) : value{v} {}
    MaybeColumnPx(RightOfScreen v) : value{v} {}

    bool left_of_screen() const {
        return std::holds_alternative<LeftOfScreen>(value);
    }

    bool right_of_screen() const {
        return std::holds_alternative<RightOfScreen>(value);
    }

    bool has_value() const {
        return std::holds_alternative<ColumnPx>(value);
    }

    explicit operator bool() const {
        return has_value();
    }

    ColumnPx & operator*() {
        return std::get<ColumnPx>(value);
    }

    ColumnPx const& operator*() const {
        return std::get<ColumnPx>(value);
    }

    ColumnPx * operator->() {
        return &**this;
    }

    ColumnPx const* operator->() const {
        return &**this;
    }
};

/// Has the same number of items as ColumnList. Does *not* exclude off-screen columns.
/// To skip drawing off-screen columns, fill their slot with nullopt.
struct ColumnLayout {
    RulerOrHandlePx ruler;
    std::vector<MaybeColumnPx> cols;
};

/// Compute where on-screen to draw each pattern column.
[[nodiscard]] static ColumnLayout gen_column_layout(
    PatternEditor const & self,
    doc::Document const & document
) {
    int const width_per_char = self._pattern_font_metrics.width;
    int const pad_width = width_per_char / columns::EXTRA_WIDTH_DIVISOR;

    int x_px = 0;

    // Add one extra pixel to the left of every subcolumn,
    // since it's taken up by a column/subcolumn border.
    constexpr int DIVIDER_WIDTH = 1;

    auto ruler_or_handle = [&x_px, pad_width, width_per_char] (
        boost::rational<int32_t> nchar, bool padding
    ) -> RulerOrHandlePx {
        int const chars_width = width_per_char * nchar.numerator() / nchar.denominator();

        RulerOrHandlePx col;

        col._left_px = x_px;
        if (padding) {
            x_px += pad_width;
        }

        col._center_px = x_px + chars_width / qreal(2.0);
        x_px += chars_width;

        if (padding) {
            x_px += pad_width;
        }
        col._right_px = x_px;

        return col;
    };

    auto wide_cell = [&x_px, pad_width, width_per_char] (
        SubColumn type, int nchar
    ) -> SubColumnPx {
        int const chars_width = width_per_char * nchar;

        auto sub = SubColumnPx(type);
        sub.ncell = 1;
        sub._pad_width = pad_width;

        sub._bounds_left = x_px;
        x_px += pad_width + DIVIDER_WIDTH;
        sub.cell_left_px[0] = x_px;

        sub.cell_center_px[0] = x_px + chars_width / qreal(2.0);
        x_px += chars_width;

        sub.cell_left_px[sub.ncell] = x_px;
        x_px += pad_width;
        sub._bounds_right = x_px;

        return sub;
    };

    auto many_cells = [&x_px, pad_width, width_per_char] (
        SubColumn type, CellIndex ncell
    ) -> SubColumnPx {
        release_assert(ncell > 0);
        release_assert(ncell <= SUBCOL_MAX_CELLS);

        auto sub = SubColumnPx(type);
        sub.ncell = ncell;
        sub._pad_width = pad_width;

        sub._bounds_left = x_px;
        x_px += pad_width + DIVIDER_WIDTH;

        for (uint32_t cell = 0; cell < ncell; cell++) {
            sub.cell_left_px[cell] = x_px;
            sub.cell_center_px[cell] = x_px + width_per_char / qreal(2.0);
            x_px += width_per_char;
        }

        sub.cell_left_px[sub.ncell] = x_px;
        x_px += pad_width;
        sub._bounds_right = x_px;

        return sub;
    };

    // SubColumn doesn't matter.
    RulerOrHandlePx ruler = ruler_or_handle(columns::RULER_WIDTH_CHARS, true);

    ColumnLayout column_layout{.ruler = ruler, .cols = {}};

    for (
        ChipIndex chip_index = 0;
        chip_index < document.chips.size();
        chip_index++
    ) {
        for (
            ChannelIndex channel_index = 0;
            channel_index < document.chip_index_to_nchan(chip_index);
            channel_index++
        ) {
            doc::EffColIndex n_effect_col =
                document.chip_channel_settings[chip_index][channel_index].n_effect_col;

            int const orig_left_px = x_px;

            // SubColumn doesn't matter.
            RulerOrHandlePx block_handle = ruler_or_handle({7, 6}, false);

            SubColumnLayout subcolumns;

            // Notes are 3 characters wide, but the cursor only has 1 position.
            subcolumns.push_back(wide_cell(SubColumn_::Note{}, 3));

            // TODO configurable column hiding (one checkbox per column type?)
            // Instruments hold 2 characters.
            subcolumns.push_back(many_cells(SubColumn_::Instrument{}, 2));

            // Volume width depends on the current chip and channel.
            {
                auto volume_width =
                    get_volume_digits(document, chip_index, channel_index);
                subcolumns.push_back(many_cells(SubColumn_::Volume{}, volume_width));
            }

            for (uint8_t effect_col = 0; effect_col < n_effect_col; effect_col++) {
                // Effect names hold 1 or 2 characters.
                // Effect values hold 2 characters.
                subcolumns.push_back(many_cells(
                    SubColumn_::Effect{effect_col},
                    (CellIndex) document.effect_name_chars + 2)
                );
            }

            // TODO replace off-screen columns with nullopt.
            column_layout.cols.push_back(ColumnPx{
                .chip = chip_index,
                .channel = channel_index,
                .left_px = orig_left_px,
                .right_px = x_px,
                .block_handle = block_handle,
                .subcolumns = subcolumns,
            });
        }
    }
    return column_layout;
}

// # Cursor positioning

struct SubColumnCells {
    SubColumn type;

    // Number of items the cursor can move into.
    CellIndex ncell;
};

using SubColumnList = std::vector<SubColumnCells>;

struct Column {
    ChipIndex chip;
    ChannelIndex channel;
    SubColumnList subcolumns;
};

using ColumnList = std::vector<Column>;

/// Generates order of all sub/columns // (not just visible columns)
/// for keyboard-based movement rather than rendering.
///
/// TODO add function in self for determining subcolumn visibility.
[[nodiscard]] static ColumnList gen_column_list(
    PatternEditor const & self,
    doc::Document const & document
) {
    ColumnList column_list;

    for (
        ChipIndex chip_index = 0;
        chip_index < document.chips.size();
        chip_index++
    ) {
        for (
            ChannelIndex channel_index = 0;
            channel_index < document.chip_index_to_nchan(chip_index);
            channel_index++
        ) {
            doc::EffColIndex n_effect_col =
                document.chip_channel_settings[chip_index][channel_index].n_effect_col;
            SubColumnList subcolumns;

            subcolumns.push_back({SubColumn_::Note{}, 1});

            // TODO configurable column hiding (one checkbox per column type?)
            subcolumns.push_back({SubColumn_::Instrument{}, 2});

            {
                auto volume_width =
                    get_volume_digits(document, chip_index, channel_index);
                subcolumns.push_back({SubColumn_::Volume{}, volume_width});
            }

            for (uint8_t effect_col = 0; effect_col < n_effect_col; effect_col++) {
                subcolumns.push_back(SubColumnCells {
                    SubColumn_::Effect{effect_col},
                    (CellIndex) document.effect_name_chars + 2,
                });
            }

            column_list.push_back(Column{
                .chip = chip_index,
                .channel = channel_index,
                .subcolumns = subcolumns,
            });
        }
    }

    return column_list;
}

// # Pattern drawing

// TODO bundle parameters into `ctx: Context`.
// columns, cfg, and document are identical between different drawing phases.
// inner_rect is not.
static void draw_header(
    PatternEditor & self,
    doc::Document const &document,
    ColumnLayout const & columns,
    QPainter & painter,
    QSize const inner_size
) {
    // Use standard app font for header text.
    painter.setFont(QFont{});

    GridRect inner_rect{QPoint{0, 0}, inner_size};

    // Draw the header background.
    {
        // See gradients.cpp, GradientRenderer::paint().
        // QLinearGradient's constructor takes the begin and endpoints.
        QLinearGradient grad{inner_rect.left_top(), inner_rect.left_bottom()};

        // You need to assign the color map afterwards.
        // List of QPalette colors at https://doc.qt.io/qt-5/qpalette.html#ColorRole-enum
        grad.setStops(QGradientStops{
            QPair{0., self.palette().button().color()},
            QPair{0.4, self.palette().light().color()},
            QPair{1., self.palette().button().color().darker(135)},
        });

        // Then cast it into a QBrush, and draw the background.
        painter.fillRect(inner_rect, QBrush{grad});
    }

    auto draw_header_border = [&self, &painter] (GridRect channel_rect) {
        // Draw border.
        painter.setPen(self.palette().shadow().color());
        // In 0CC, each "gray gridline" belongs to the previous (left) channel.
        // In our tracker, each "gray gridline" belongs to the next channel.
        // But draw the header the same as 0CC, it looks prettier.
        draw_top_border(painter, channel_rect);
        draw_right_border(painter, channel_rect);
        draw_bottom_border(painter, channel_rect);

        // Draw highlight.
        int pen_width = painter.pen().width();

        GridRect inner_rect{channel_rect};
        inner_rect.x2() -= pen_width;
        inner_rect.y1() += pen_width;
        inner_rect.y2() -= pen_width;

        painter.setPen(self.palette().light().color());
        draw_top_border(painter, inner_rect);
        draw_left_border(painter, inner_rect);
    };

    // Draw the ruler's header outline.
    {
        GridRect channel_rect{inner_rect};
        channel_rect.set_left(columns.ruler.left_px());
        channel_rect.set_right(columns.ruler.right_px());

        // Unlike other channels, the ruler has no black border to its left.
        // So draw it manually.
        painter.setPen(self.palette().shadow().color());
        draw_left_border(painter, channel_rect);

        int pen_width = painter.pen().width();
        channel_rect.x1() += pen_width;

        draw_header_border(channel_rect);
    }

    // Draw each channel's header outline and text.
    for (MaybeColumnPx const & maybe_column : columns.cols) {
        if (!maybe_column) {
            continue;
        }
        ColumnPx const & column = *maybe_column;

        auto chip = column.chip;
        auto channel = column.channel;

        GridRect channel_rect{inner_rect};
        channel_rect.set_left(column.left_px);
        channel_rect.set_right(column.right_px);

        PainterScope scope{painter};

        // Prevent painting out of bounds.
        painter.setClipRect(channel_rect);

        // Adjust the coordinate system to place this object at (0, 0).
        painter.translate(channel_rect.left_top());
        channel_rect.move_top(0);
        channel_rect.move_left(0);

        // Draw text.
        painter.setPen(self.palette().text().color());
        painter.drawText(
            header::TEXT_X,
            header::TEXT_Y,
            QString(channel_name(document, chip, channel)));

        draw_header_border(channel_rect);
    }
}


namespace {

// # Utility functions:

Cursor const& get_cursor(PatternEditor const& widget) {
    return widget._win._state.cursor();
}

std::optional<Selection> get_select(PatternEditor const& widget) {
    return widget._win._state.select();
}

std::optional<RawSelection> get_raw_sel(PatternEditor const& widget) {
    return widget._win._state.raw_select();
}

// # Pattern drawing:

// yay inconsistency
using PxInt = int;
//using PxNat = uint32_t;

/// Convert a relative timestamp to a vertical display offset.
PxInt pixels_from_beat(PatternEditor const & widget, BeatFraction beat) {
    PxInt out = doc::round_to_int(
        beat * widget._zoom_level * widget._pixels_per_row
    );
    return out;
}

struct GridCellPosition {
    GridIndex grid;
    // top and bottom lie on gridlines like GridRect, not pixels like QRect.
    PxInt top;
    PxInt bottom;
    bool focused;
};

struct PatternPosition {
    GridIndex grid;
    // top and bottom lie on gridlines like GridRect, not pixels like QRect.
    PxInt top;
    PxInt bottom;
    bool focused;
};

enum class Direction {
    Forward,
    Reverse,
};

/// Stores the location of a grid cell on-screen.
struct GridCellIteratorState {
    PatternEditor const & _widget;
    doc::Document const & _document;

    // Screen pixels (non-negative, but mark as signed to avoid conversion errors)
    static constexpr PxInt _screen_top = 0;
    const PxInt _screen_bottom;

    // Initialized from _scroll_position.
    GridIndex _curr_grid_index;
    PxInt _curr_grid_pos;  // Represents top if Forward, bottom if Reverse.

    bool _focused;

    // impl
    static PxInt centered_cursor_pos(PxInt screen_height) {
        return screen_height / 2;
    }

public:
    static std::tuple<GridCellIteratorState, PxInt> make(
        PatternEditor const & widget,  // holds reference
        doc::Document const & document,  // holds reference
        PxInt const screen_height
    ) {
        GridAndBeat cursor_y = get_cursor(widget).y;
        PxInt const cursor_from_pattern_top =
            pixels_from_beat(widget, cursor_y.beat);

        GridAndBeat scroll_position;
        PxInt pattern_top_from_screen_top;
        PxInt cursor_from_screen_top;

        if (widget._free_scroll_position.has_value()) {
            // Free scrolling.
            scroll_position = *widget._free_scroll_position;

            PxInt const screen_top_from_pattern_top =
                pixels_from_beat(widget, scroll_position.beat);
            pattern_top_from_screen_top = -screen_top_from_pattern_top;
            cursor_from_screen_top =
                cursor_from_pattern_top + pattern_top_from_screen_top;
        } else {
            // Cursor-locked scrolling.
            scroll_position = cursor_y;

            cursor_from_screen_top = centered_cursor_pos(screen_height);
            pattern_top_from_screen_top =
                cursor_from_screen_top - cursor_from_pattern_top;
        }

        GridCellIteratorState out {
            widget,
            document,
            screen_height,
            scroll_position.grid,
            pattern_top_from_screen_top,
            // This WILL break if _free_scroll_position exists
            // and lies in a different grid than the cursor
            // (which 0CC-FT deliberately prevents from happening).
            true,
        };
        return {out, cursor_from_screen_top};
    }
};

/// Iterates up or down through patterns, and yields their locations on-screen.
template<Direction direction>
class GridCellIterator : private GridCellIteratorState {
    // impl
public:
    explicit GridCellIterator(GridCellIteratorState state) :
        GridCellIteratorState{state}
    {
        if constexpr (direction == Direction::Reverse) {
            _curr_grid_index--;
            _focused = false;
        }
    }

private:
    bool valid_grid_cell() const {
        return (size_t) _curr_grid_index < _document.timeline.size();
    }

    /// Precondition: valid_grid_cell() is true.
    inline PxInt curr_grid_height() const {
        return pixels_from_beat(
            _widget, _document.timeline[_curr_grid_index].nbeats
        );
    }

    /// Precondition: valid_grid_cell() is true.
    inline PxInt curr_grid_top() const {
        if constexpr (direction == Direction::Forward) {
            return _curr_grid_pos;
        } else {
            return _curr_grid_pos - curr_grid_height();
        }
    }

    /// Precondition: valid_grid_cell() is true.
    inline PxInt curr_grid_bottom() const {
        if constexpr (direction == Direction::Reverse) {
            return _curr_grid_pos;
        } else {
            return _curr_grid_pos + curr_grid_height();
        }
    }

private:
    inline GridCellPosition peek() const {
        return GridCellPosition {
            .grid = _curr_grid_index,
            .top = curr_grid_top(),
            .bottom = curr_grid_bottom(),
            .focused = _focused,
        };
    }

public:
    std::optional<GridCellPosition> next() {
        if constexpr (direction == Direction::Forward) {
            if (!valid_grid_cell() || _curr_grid_pos >= _screen_bottom) {
                return std::nullopt;
            }

            GridCellPosition out = peek();
            _curr_grid_pos += curr_grid_height();
            _curr_grid_index++;
            _focused = false;
            return out;

        } else {
            if (!valid_grid_cell() || _curr_grid_pos <= _screen_top) {
                return std::nullopt;
            }

            GridCellPosition out = peek();
            _curr_grid_pos -= curr_grid_height();
            _curr_grid_index--;  // May overflow to UINT32_MAX. Not UB.
            _focused = false;
            return out;
        }
    }
};

/// Iterates through all patterns visible on-screen, both above and below cursor.
/// Calls the callback with the on-screen pixel coordinates of the pattern.
class ForeachGrid {
    GridCellIteratorState const& _state;

public:
    ForeachGrid(GridCellIteratorState const& state) : _state{state} {}

    template<typename Visitor>
    void operator()(Visitor visit_grid_cell) const {
        {
            auto forward = GridCellIterator<Direction::Forward>{_state};
            while (std::optional<GridCellPosition> pos = forward.next()) {
                visit_grid_cell(*pos);
            }
        }
        {
            auto reverse = GridCellIterator<Direction::Reverse>{_state};
            while (auto pos = reverse.next()) {
                visit_grid_cell(*pos);
            }
        }
    }
};

}

static QLinearGradient make_gradient(
    int cursor_top, int cursor_bottom,  QColor color, int top_alpha, int bottom_alpha
) {
    // QLinearGradient's constructor takes the begin and endpoints.
    QLinearGradient grad{QPoint{0, cursor_top}, QPoint{0, cursor_bottom}};

    // You need to assign the color map afterwards.
    QColor top_color{color};
    top_color.setAlpha(top_alpha);

    QColor bottom_color{color};
    bottom_color.setAlpha(bottom_alpha);

    grad.setStops(QGradientStops{
        QPair{0., top_color},
        QPair{1., bottom_color},
    });

    return grad;
}

using CellIter = doc::TimelineCellIterRef;

/// Computing colors may require blending with the background color.
/// So cache the color for each timeline entry being drawn.
#define CACHE_COLOR(COLOR) \
    QColor COLOR = visual.COLOR(pos.focused);

/// Draw the background lying behind notes/etc.
static void draw_pattern_background(
    PatternEditor & self,
    doc::Document const &document,
    ColumnLayout const & columns,
    QPainter & painter,
    QSize const inner_size
) {
    auto & visual = get_app().options().visual;

    int row_right_px = columns.ruler.right_px();
    for (auto & c : reverse(columns.cols)) {
        if (c.has_value()) {
            row_right_px = c->right_px;
            break;
        }
    }

    auto const [seq, cursor_top] =
        GridCellIteratorState::make(self, document, (PxInt) inner_size.height());
    ForeachGrid foreach_grid{seq};

    auto draw_pattern_bg = [&] (GridCellPosition const & pos) {
        doc::TimelineRow const & grid_cell = document.timeline[pos.grid];

        #define CACHE_SUBCOLUMN_COLOR(OUT) \
            QColor OUT##_divider = visual.OUT##_divider(pos.focused); \
            QColor OUT##_bg = visual.OUT##_bg(pos.focused);

        CACHE_SUBCOLUMN_COLOR(note);
        CACHE_SUBCOLUMN_COLOR(instrument);
        CACHE_SUBCOLUMN_COLOR(volume);
        CACHE_SUBCOLUMN_COLOR(effect);

        CACHE_COLOR(gridline_beat);
        CACHE_COLOR(gridline_non_beat);

        // Draw background columns.
        for (MaybeColumnPx const & maybe_column : columns.cols) {
            if (!maybe_column) {
                continue;
            }
            for (SubColumnPx const & sub : maybe_column->subcolumns) {
                GridRect sub_rect{
                    QPoint{sub.left_px(), pos.top}, QPoint{sub.right_px(), pos.bottom}
                };

                // Unrecognized columns are red to indicate an error.
                // This shouldn't happen, but whatever.
                QColor bg{255, 0, 0};
                QColor fg{QColor::Invalid};

                #define CASE(VARIANT, BG, FG) \
                    if (std::holds_alternative<VARIANT>(sub.type)) { \
                        bg = BG; \
                        fg = FG; \
                    }
                #define CASE_NO_FG(VARIANT, BG) \
                    if (std::holds_alternative<VARIANT>(sub.type)) { \
                        bg = BG; \
                    }

                namespace sc = SubColumn_;

                // Don't draw the note column's divider line,
                // since it lies right next to the previous channel's channel divider.
                CASE(sc::Note, note_bg, note_divider)
                CASE(sc::Instrument, instrument_bg, instrument_divider)
                CASE(sc::Volume, volume_bg, volume_divider)
                CASE(sc::Effect, effect_bg, effect_divider)

                #undef CASE
                #undef CASE_NO_FG

                // Paint background color.
                painter.fillRect(sub_rect, bg);

                // Paint left border.
                if (fg.isValid()) {
                    painter.setPen(fg);
                    draw_left_border(painter, sub_rect);
                }
            }
        }

        // Draw rows.
        // Begin loop(row)
        int row = 0;
        BeatFraction const beats_per_row{1, self._zoom_level};
        BeatFraction curr_beats = 0;
        for (;
            curr_beats < grid_cell.nbeats;
            curr_beats += beats_per_row, row += 1)
        {
            // Compute row height.
            int ytop = pos.top + self._pixels_per_row * row;
            // End loop(row)

            // Draw gridline along top of row.
            if (curr_beats.denominator() == 1) {
                painter.setPen(gridline_beat);
            } else {
                painter.setPen(gridline_non_beat);
            }
            draw_top_border(painter, QPoint{0, ytop}, QPoint{row_right_px, ytop});
        }
    };

    foreach_grid(draw_pattern_bg);

    /// Draw divider "just past right" of each column.
    /// This replaces the "note divider" of the next column.
    /// The last column draws a divider in the void.
    auto draw_divider = [&painter, &inner_size] (int x) {
        QPoint right_top{x, 0};
        QPoint right_bottom{x, inner_size.height()};

        draw_left_border(painter, right_top, right_bottom);
    };

    painter.setPen(visual.channel_divider);

    draw_divider(columns.ruler.right_px());
    for (auto & column : columns.cols) {
        if (column) {
            draw_divider(column->right_px);
        }
    }

    auto pattern_draw_blocks = [&] (
        ColumnPx const & column, PatternPosition const & pos, doc::PatternRef pattern
    ) {
        PainterScope scope{painter};
        painter.translate(0, pos.top);

        // Draw block handle.
        auto sub = column.block_handle;
        GridRect sub_rect{
            QPoint{sub.left_px(), 0},
            QPoint{sub.right_px() + painter.pen().width(), pos.bottom - pos.top}
        };

        // Draw background.
        using gui::lib::color::lerp_colors;

        auto base = visual.block_handle(pos.focused);
        auto border = visual.block_handle_border(pos.focused);

        painter.fillRect(sub_rect, base);

        // Draw frame.
        painter.setPen(border);
        draw_left_border(painter, sub_rect);

        if (pattern.is_block_begin) {
            draw_top_border(painter, sub_rect);

        } else {
            // Draw loop indicator triangles.

            qreal x0 = sub.left_px() + painter.pen().width();
            qreal x1 = sub.right_px();
            qreal y0 = painter.pen().widthF() * 0.5;

            auto width = x1 - x0;
            auto dx = width / qreal(3.0);
            auto dy = width / qreal(3.0);

            QPolygonF left_tri;
            left_tri
                << QPointF(x0 + 0 , y0 - dy)
                << QPointF(x0 + dx, y0 + 0 )
                << QPointF(x0 + 0 , y0 + dy);

            QPolygonF right_tri;
            right_tri
                << QPointF(x1 - 0 , y0 - dy)
                << QPointF(x1 - dx, y0 + 0 )
                << QPointF(x1 - 0 , y0 + dy);

            PainterScope scope{painter};

            painter.setPen({});
            painter.setBrush(border);
            painter.setRenderHint(QPainter::Antialiasing);

            painter.drawPolygon(left_tri);
            painter.drawPolygon(right_tri);
        }

        painter.setPen(border);
        if (pattern.is_block_end) {
            draw_bottom_border(painter, sub_rect);
        }
        // Should this be drawn or not?
        draw_right_border(painter, sub_rect);
    };

    foreach_grid([&self, &columns, &document, &pattern_draw_blocks] (
        GridCellPosition const& pos
    ) {
        for (auto const& maybe_col : columns.cols) {
            if (!maybe_col) continue;
            ColumnPx const& col = *maybe_col;

            auto timeline =
                doc::TimelineChannelRef(document.timeline, col.chip, col.channel);
            auto iter = CellIter(timeline[pos.grid]);

            while (auto p = iter.next()) {
                auto pattern = *p;
                PxInt top = pos.top + pixels_from_beat(self, pattern.begin_time);
                PxInt bottom = pos.top + pixels_from_beat(self, pattern.end_time);

                PatternPosition pattern_pos {
                    .grid = pos.grid,
                    .top = top,
                    .bottom = bottom,
                    .focused = pos.focused,
                };

                pattern_draw_blocks(col, pattern_pos, *p);
            }
        }
    });

    // Draw selection.
    if (auto maybe_select = get_select(self)) {
        auto select = *maybe_select;

        // Limit selections to patterns, not ruler.
        PainterScope scope{painter};
        painter.setClipRect(GridRect::from_corners(
            columns.ruler.right_px(), 0, inner_size.width(), inner_size.height()
        ));

        int off_screen = std::max(inner_size.width(), inner_size.height()) + 100;

        using MaybePxInt = std::optional<PxInt>;

        /// Overwritten with the estimated top/bottom of the selection on-screen.
        /// Set to ±off_screen if selection endpoint is above or below screen.
        ///
        /// These values are initialized to nullopt,
        /// and overwritten with a non-null value on the first call to calc_select_pos().
        MaybePxInt maybe_select_top{};
        MaybePxInt maybe_select_bottom{};

        /// Every time we compare a selection endpoint against a timeline entry / grid cell,
        /// we can identify if it's within, above, or below it.
        ///
        /// If within, overwrite `select_px` unconditionally.
        /// If above/below, overwrite `select_px` if it's nullopt.
        ///
        /// Once all on-screen grid cells have been checked, `select_px` holds
        /// either a pixel value (if it's located in an on-screen grid cell),
        /// or ±off_screen (if it's located in an off-screen grid cell).
        auto calc_select_pos = [&self, off_screen] (
            GridAndBeat select_time,
            GridCellPosition const& grid_px,
            MaybePxInt &/*mut*/ select_px
        ) {
            using Frac = BeatFraction;

            // If time is in current grid cell, set exact position.
            // This overwrites ±off_screen, and will not be replaced with ±off_screen.
            if (select_time.grid == grid_px.grid) {
                Frac select_row = select_time.beat * self._zoom_level;
                PxInt select_y = doc::round_to_int(self._pixels_per_row * select_row);

                select_px = grid_px.top + select_y;
                return;
            }

            // If time is above current grid cell,
            // set to -off_screen (if no value present).
            if (select_time.grid < grid_px.grid) {
                select_px = select_px.value_or(-off_screen);
                return;
            }

            // If time is below current grid cell,
            // set to +off_screen (if no value present).
            release_assert(select_time.grid > grid_px.grid);
            select_px = select_px.value_or(+off_screen);
        };

        foreach_grid([
            &select, &calc_select_pos,
            &/*mut*/ maybe_select_top, &/*mut*/ maybe_select_bottom
        ] (GridCellPosition const& grid_px) {
            calc_select_pos(select.top, grid_px, maybe_select_top);
            calc_select_pos(select.bottom, grid_px, maybe_select_bottom);
        });

        // calc_select_pos() is called once for every on-screen timeline entry.
        // These variables are overwritten on the first call to calc_select_pos().
        // It should be impossible to scroll the screen (or move the cursor)
        // such that 0 timeline entries are drawn.
        if (!maybe_select_top || !maybe_select_bottom) {
            throw std::logic_error("Trying to draw selection with 0 patterns");
        }

        PxInt & select_top = *maybe_select_top;
        PxInt & select_bottom = *maybe_select_bottom;

        release_assert(select_top <= select_bottom);

        auto calc_select_x = [&] (CursorX x, bool right_border) {
            auto const& c = columns.cols[x.column];
            if (c.has_value()) {
                SubColumnPx sc = c->subcolumns[x.subcolumn];

                // In FamiTracker, subcolumn boundaries determine selection borders.
                // They are slightly larger than the character drawing regions
                // (which determine cursor borders).
                return right_border ? sc.right_px() : sc.left_px();
            }
            if (c.left_of_screen()) {
                return -off_screen;
            }
            if (c.right_of_screen()) {
                return +off_screen;
            }
            throw std::logic_error(
                fmt::format("column {} is missing a position", x.column)
            );
        };

        PxInt select_left = calc_select_x(select.left, false);
        PxInt select_right = calc_select_x(select.right, true);

        if (select_top != select_bottom) {
            auto select_rect = GridRect::from_corners(
                select_left, select_top, select_right, select_bottom
            );

            // TODO use different color for selections in focused and unfocused grids.
            painter.fillRect(select_rect, visual.select_bg(true));

            painter.setPen(visual.select_border(true));
            draw_left_border(painter, select_rect);
            draw_right_border(painter, select_rect);
            draw_top_border(painter, select_rect);
            draw_bottom_border(painter, select_rect);

        } else {
            int select_grad_bottom = select_top + self._pixels_per_row * 2 / 3;
            auto select_rect = GridRect::from_corners(
                select_left, select_top, select_right, select_grad_bottom
            );

            auto select_grad = make_gradient(
                select_top, select_grad_bottom, visual.select_bg(true), 255, 0
            );
            painter.fillRect(select_rect, select_grad);

            auto border_grad = make_gradient(
                select_top, select_grad_bottom, visual.select_border(true), 255, 0
            );
            painter.fillRect(top_border(painter, select_rect), border_grad);
            painter.fillRect(left_border(painter, select_rect), border_grad);
            painter.fillRect(right_border(painter, select_rect), border_grad);
        }
    }

    // Draw cursor gradient after drawing the divider.
    // The cursor row is drawn on top of the divider,
    // so the gradient should be too.
    {
        int cursor_bottom = cursor_top + self._pixels_per_row;

        GridRect cursor_row_rect{
            QPoint{0, cursor_top}, QPoint{row_right_px, cursor_bottom}
        };

        auto bg_grad = make_gradient(
            cursor_top,
            cursor_bottom,
            self._edit_mode ? visual.cursor_row_edit : visual.cursor_row,
            visual.cursor_top_alpha,
            visual.cursor_bottom_alpha
        );

        auto cursor_x = get_cursor(self).x;
        if (cursor_x.column >= columns.cols.size()) {
            cursor_x.column = 0;
            cursor_x.subcolumn = 0;
        }

        // Draw background for cursor row and cell.
        if (auto & col = columns.cols[cursor_x.column]) {
            // If cursor is on-screen, draw left/cursor/right.
            auto subcol = col->subcolumns[cursor_x.subcolumn];
            auto [cell_left, cell_right] = subcol.cell_left_right(cursor_x.cell);

            // Draw gradient (space to the left of the cursor cell).
            auto left_rect = cursor_row_rect;
            left_rect.set_right(cell_left);
            painter.fillRect(left_rect, bg_grad);

            // Draw gradient (space to the right of the cursor cell).
            auto right_rect = cursor_row_rect;
            right_rect.set_left(cell_right);
            painter.fillRect(right_rect, bg_grad);

            // Draw gradient (cursor cell only).
            GridRect cursor_rect{
                QPoint{cell_left, cursor_top},
                QPoint{cell_right, cursor_bottom}
            };
            painter.fillRect(
                cursor_rect,
                make_gradient(
                    cursor_top,
                    cursor_bottom,
                    visual.cell,
                    visual.cell_top_alpha,
                    visual.cell_bottom_alpha
                )
            );
        } else {
            // Otherwise draw a single gradient.
            painter.fillRect(cursor_row_rect, bg_grad);
        }
    }

    auto draw_row_numbers = [&] (GridCellPosition const & pos) {
        auto const & grid_cell = document.timeline[pos.grid];
        CACHE_COLOR(note_line_beat);

        // Draw rows.
        // Begin loop(row)
        int row = 0;
        BeatFraction const beats_per_row{1, self._zoom_level};
        BeatFraction curr_beats = 0;
        for (;
            curr_beats < grid_cell.nbeats;
            curr_beats += beats_per_row, row += 1)
        {
            int ytop = pos.top + self._pixels_per_row * row;

            // Draw ruler labels (numbers).
            if (curr_beats.denominator() == 1) {
                // Draw current beat.
                QString s = format_hex_2((uint8_t) curr_beats.numerator());

                painter.setFont(visual.pattern_font);
                painter.setPen(note_line_beat);

                DrawText draw_text{visual.pattern_font};
                draw_text.draw_text(
                    painter,
                    columns.ruler.center_px(),
                    ytop + visual.font_tweaks.pixels_above_text,
                    Qt::AlignTop | Qt::AlignHCenter,
                    s
                );
            }
            // Don't label non-beat rows for the time being.
        }
    };

    foreach_grid(draw_row_numbers);
}

/// Draw `RowEvent`s positioned at TimeInPattern. Not all events occur at beat boundaries.
static void draw_pattern_foreground(
    PatternEditor & self,
    doc::Document const &document,
    ColumnLayout const & columns,
    QPainter & painter,
    QSize const inner_size
) {
    using Frac = BeatFraction;

    auto & visual = get_app().options().visual;
    auto & note_cfg = get_app().options().note_names;

    // Take a backup of _image to self._temp_image.
    {
        QPainter temp_painter{&self._temp_image};
        temp_painter.drawImage(0, 0, self._image);
    }

    painter.setFont(visual.pattern_font);
    DrawText text_painter{painter.font()};

    // Dimensions of the note cut/release rectangles.
    int const rect_height = std::max(qRound(self._pixels_per_row / 8.0), 2);
    qreal const rect_width = 2.25 * self._pattern_font_metrics.width;

    // Shift the rectangles vertically a bit, when rounding off sizes.
    constexpr qreal Y_OFFSET = 0.0;

    auto draw_note_cut = [&self, &painter, rect_height, rect_width] (
        SubColumnPx const & subcolumn, QColor color
    ) {
        qreal x1f = subcolumn.center_px() - rect_width / 2;
        qreal x2f = x1f + rect_width;
        x1f = round(x1f);
        x2f = round(x2f);

        // Round to integer, so note release has integer gap between lines.
        painter.setPen(QPen{color, qreal(rect_height)});

        qreal y = self._pixels_per_row * qreal(0.5) + Y_OFFSET;
        painter.drawLine(QPointF{x1f, y}, QPointF{x2f, y});
    };

    auto draw_release = [&self, &painter, rect_height, rect_width] (
        SubColumnPx const & subcolumn, QColor color
    ) {
        qreal x1f = subcolumn.center_px() - rect_width / 2;
        qreal x2f = x1f + rect_width;
        int x1 = qRound(x1f);
        int x2 = qRound(x2f);

        // Round to integer, so note release has integer gap between lines.
        painter.setPen(QPen{color, qreal(rect_height)});

        int ytop = qRound(0.5 * self._pixels_per_row - 0.5 * rect_height + Y_OFFSET);
        int ybot = ytop + rect_height;

        draw_bottom_border(painter, GridRect::from_corners(x1, ytop, x2, ytop));
        draw_top_border(painter, GridRect::from_corners(x1, ybot, x2, ybot));
    };

    auto pattern_draw_notes = [&] (
        ColumnPx const & column, PatternPosition const & pos, doc::PatternRef pattern
    ) {
        CACHE_COLOR(note_line_beat);
        CACHE_COLOR(note_line_non_beat);
        CACHE_COLOR(note_line_fractional);
        CACHE_COLOR(instrument);
        CACHE_COLOR(volume);
        CACHE_COLOR(effect);

        PainterScope scope{painter};

        painter.setClipRect(
            GridRect(
                QPoint(column.left_px, pos.top), QPoint(column.right_px, pos.bottom)
            ),
            Qt::IntersectClip
        );

        // Right now, only draw_pattern_foreground() and not draw_pattern_background()
        // calls translate(pos.top).
        // This should be made consistent so it's easier to copy code between them.
        painter.translate(0, pos.top);

        // https://bugs.llvm.org/show_bug.cgi?id=33236
        // the original C++17 spec broke const struct unpacking.
        for (doc::TimedRowEvent timed_event : pattern.events) {
            Frac beat = timed_event.anchor_beat;
            doc::RowEvent row_event = timed_event.v;

            // Compute where to draw row.
            Frac row = beat * self._zoom_level;
            int yPx = doc::round_to_int(self._pixels_per_row * row);

            // Move painter relative to current row (not cell).
            PainterScope scope{painter};
            painter.translate(0, yPx);

            // TODO add coarse/fine highlight fractions
            QColor note_color;

            if (beat.denominator() == 1) {
                // Highlighted notes
                note_color = note_line_beat;
            } else if (row.denominator() == 1) {
                // Non-highlighted notes
                note_color = note_line_non_beat;
            } else {
                // Off-grid misaligned notes (not possible in traditional trackers)
                note_color = note_line_fractional;
            }

            auto draw_top_line = [&painter, &note_color] (
                SubColumnPx const& sub, int left_offset = 0
            ) {
                QPoint left_top{sub.left_px() + left_offset, 0};
                QPoint right_top{sub.right_px(), 0};

                // Draw top border. Do it after each note clears the background.
                painter.setPen(note_color);
                draw_top_border(painter, left_top, right_top);
            };

            // Draw text.
            for (auto const & subcolumn : column.subcolumns) {
                namespace sc = SubColumn_;

                PainterScope scope{painter};

                // Prevent text drawing from drawing into adjacent subcolumns.
                painter.setClipRect(
                    GridRect{
                        QPoint{subcolumn.left_px(), 0},
                        // Double the height so descenders can still draw into the next row.
                        // Is this a good idea? IDK.
                        QPoint{subcolumn.right_px(), 2 * self._pixels_per_row},
                    },
                    Qt::IntersectClip
                );

                auto clear_subcolumn = [&self, &painter, &subcolumn] () {
                    // Clear background using unmodified copy free of rendered text.
                    // Unlike alpha transparency, this doesn't break ClearType
                    // and may be faster as well.

                    // One concern is that with some fonts and `pixels_below_text` settings,
                    // long Q tails may not be cleared fully.
                    // If this happens, multiply clear_height by 1.5 or 2-ish,
                    // or change calc_single_font_metrics and calc_font_metrics
                    // to save the actual descent height
                    // (based on visual.font_tweaks.pixels_below_text).

                    auto clear_height = self._pixels_per_row;

                    GridRect target_rect{
                        QPoint{subcolumn.left_px(), 0},
                        QPoint{subcolumn.right_px(), clear_height},
                    };
                    auto sample_rect = painter.combinedTransform().mapRect(target_rect);
                    painter.drawImage(target_rect, self._temp_image.copy(sample_rect));
                };

                /// Draw a single character centered at a specific X-coordinate.
                auto draw_char = [
                    &visual, &text_painter, &painter
                ] (QChar single_char, qreal char_center_x) {
                    // Text is being drawn relative to top-left of current row (not cell).
                    // subcolumn.cell_center_px[] is relative to screen left (not cell).
                    text_painter.draw_text(
                        painter,
                        char_center_x,
                        visual.font_tweaks.pixels_above_text,
                        Qt::AlignTop | Qt::AlignHCenter,
                        QString(single_char)
                    );
                };

                /// Draw a string of characters,
                /// each centered at a different cell's X-coordinate.
                /// Used for printing fixed-length strings into a series of cells.
                auto draw_cells = [&draw_char](
                    QString const& text, gsl::span<qreal const> cell_centers
                ) {
                    int nchar = text.size();
                    release_assert_equal((size_t) nchar, cell_centers.size());

                    for (int i = 0; i < nchar; i++) {
                        draw_char(text[i], cell_centers[(size_t) i]);
                    }
                };

                /// Draw an arbitrary-length string of characters,
                /// centered at a single cell's X-coordinate.
                /// All characters are spaced out at equal intervals,
                /// even if the font is not monospace.
                auto draw_text = [
                    &draw_char, width_per_char = self._pattern_font_metrics.width
                ] (QString const& text, qreal center_x) {
                    int nchar = text.size();
                    if (!(nchar >= 1)) return;

                    // Compute the center x of the leftmost character.
                    qreal char_center_x = center_x - (nchar - 1) * width_per_char / qreal(2);

                    // One would think you could draw a character using a QPainter
                    // without performing a heap allocation...
                    // but QPainter::drawText() doesn't seem to allow it.
                    for (
                        int i = 0;
                        i < nchar;
                        i++, char_center_x += width_per_char
                    ) {
                        draw_char(text[i], char_center_x);
                    }
                };

                /// Like draw_text(), except the text is drawn at its natural width
                /// (instead of monospace), and compressed horizontally and vertically
                /// to approximately fit in max_width_char.
                auto draw_text_squash = [
                    &painter,
                    &text_painter,
                    &visual,
                    pixels_per_row = self._pixels_per_row,
                    orig_width_per_char = self._pattern_font_metrics.width]
                (
                    QString const& text,
                    qreal center_x,
                    qreal y_scale,
                    qreal max_width_char)
                {
                    PainterScope scope{painter};

                    // We use draw_text() and specify the top pixel of the resulting text.
                    // When we shrink the text vertically,
                    // we need to move the top pixel downwards to keep the text centered.
                    qreal y_shrink = pixels_per_row * (1 - y_scale);
                    painter.translate(center_x, y_shrink / 2);

                    // Compress the text so it fits within `max_width_char`.
                    QRectF bounding_rect;
                    text_painter.draw_text(
                        painter,
                        0,
                        visual.font_tweaks.pixels_above_text,
                        Qt::AlignTop | Qt::AlignHCenter | Qt::TextDontPrint,
                        text,
                        &/*out*/ bounding_rect
                    );
                    qreal text_w = bounding_rect.width();
                    qreal max_w = orig_width_per_char * max_width_char;
                    qreal x_scale = qMin(y_scale, max_w / text_w);

                    // Shrink the text horizontally and vertically.
                    painter.scale(x_scale, y_scale);

                    text_painter.draw_text(
                        painter,
                        0,
                        visual.font_tweaks.pixels_above_text,
                        Qt::AlignTop | Qt::AlignHCenter,
                        text
                    );
                };


                #define CASE(VARIANT) \
                    if (std::holds_alternative<VARIANT>(subcolumn.type))

                CASE(sc::Note) {
                    if (row_event.note) {
                        auto note = *row_event.note;
                        clear_subcolumn();

                        if (note.is_cut()) {
                            draw_note_cut(subcolumn, note_color);
                        } else if (note.is_release()) {
                            draw_release(subcolumn, note_color);
                        } else {
                            painter.setPen(note_color);

                            QString s = is_noise(document, column.chip, column.channel)
                                ? format::format_pattern_noise(note)
                                : format::format_pattern_note(
                                    note_cfg, document.accidental_mode, note
                                );

                            draw_text(s, subcolumn.center_px());
                        }

                        draw_top_line(subcolumn, painter.pen().width());
                    }
                }
                CASE(sc::Instrument) {
                    if (row_event.instr) {
                        clear_subcolumn();

                        painter.setPen(instrument);
                        auto s = format_hex_2(*row_event.instr);
                        draw_cells(s, subcolumn.cell_centers());

                        draw_top_line(subcolumn);
                    }
                }

                CASE(sc::Volume) {
                    if (row_event.volume) {
                        clear_subcolumn();

                        painter.setPen(volume);
                        auto s = subcolumn.ncell == 2
                            ? format_hex_2(*row_event.volume)
                            : format_hex_1(*row_event.volume);
                        draw_cells(s, subcolumn.cell_centers());

                        draw_top_line(subcolumn);
                    }
                }

                if (auto p = std::get_if<SubColumn_::Effect>(&subcolumn.type)) {
                    release_assert(p->effect_col < doc::MAX_EFFECTS_PER_EVENT);
                    auto const& eff = row_event.effects[p->effect_col];

                    if (eff) {
                        clear_subcolumn();

                        auto const& name_arr = eff->name;
                        QString name = QString(name_arr[0]) + QChar(name_arr[1]);
                        QString value = format_hex_2(eff->value);

                        auto center_pxs = subcolumn.cell_centers();

                        if (center_pxs.size() == 4) {
                            // Effect names are shown as 2 characters/cells wide.
                            assert(document.effect_name_chars == 2);

                            painter.setPen(effect);
                            draw_cells(name, center_pxs.subspan(0, 2));

                            painter.setPen(note_color);
                            draw_cells(value, center_pxs.subspan(2));

                        } else {
                            // Effect names are shown as 1 character/cell wide.
                            assert(center_pxs.size() == 3);
                            assert(document.effect_name_chars == 1);

                            painter.setPen(effect);
                            if (name[0] == doc::EFFECT_NAME_PLACEHOLDER) {
                                // The effect name is 0X, so only show X.
                                draw_char(name[1], center_pxs[0]);
                            } else {
                                // The effect name is XY, so show both characters.
                                // Reduce character width to minimize overflowing
                                // from its cell.
                                draw_text_squash(
                                    name,
                                    // HACK: fonts look better-aligned when drawn further to the left.
                                    center_pxs[0] - 1,
                                    0.9,  // y_scale
                                    1.2  // max_width_char
                                );
                            }

                            painter.setPen(note_color);
                            draw_cells(value, center_pxs.subspan(1));
                        }

                        draw_top_line(subcolumn);
                    }
                }

                #undef CASE
            }
        }
    };

    auto const [seq, cursor_top] =
        GridCellIteratorState::make(self, document, (PxInt) inner_size.height());
    ForeachGrid foreach_grid{seq};

    foreach_grid([&self, &columns, &document, &pattern_draw_notes] (
        GridCellPosition const& pos
    ) {
        for (auto const& maybe_col : columns.cols) {
            if (!maybe_col) continue;
            ColumnPx const& col = *maybe_col;

            auto timeline =
                doc::TimelineChannelRef(document.timeline, col.chip, col.channel);
            auto iter = CellIter(timeline[pos.grid]);

            while (auto p = iter.next()) {
                auto pattern = *p;
                PxInt top = pos.top + pixels_from_beat(self, pattern.begin_time);
                PxInt bottom = pos.top + pixels_from_beat(self, pattern.end_time);

                PatternPosition pattern_pos {
                    .grid = pos.grid,
                    .top = top,
                    .bottom = bottom,
                    .focused = pos.focused,
                };

                pattern_draw_notes(col, pattern_pos, *p);
            }
        }
    });

    // Draw cursor.
    // The cursor is drawn on top of channel dividers and note lines/text.
    {
        int cursor_bottom = cursor_top + self._pixels_per_row;

        int row_right_px = columns.ruler.right_px();
        for (auto & c : reverse(columns.cols)) {
            if (c.has_value()) {
                row_right_px = c->right_px;
                break;
            }
        }

        // Draw white line across entire screen.
        painter.setPen(self._edit_mode ? visual.cursor_row_edit : visual.cursor_row);
        draw_top_border(
            painter, QPoint{0, cursor_top}, QPoint{row_right_px, cursor_top}
        );

        // Draw cursor cell outline:
        auto cursor_x = get_cursor(self).x;

        // If cursor is on-screen, draw cell outline.
        if (auto & col = columns.cols[cursor_x.column]) {
            auto subcol = col->subcolumns[cursor_x.subcolumn];
            auto [cell_left, cell_right] = subcol.cell_left_right(cursor_x.cell);

            GridRect cursor_rect{
                QPoint{cell_left, cursor_top},
                QPoint{cell_right, cursor_bottom}
            };

            // Draw top line.
            painter.setPen(visual.cell);
            draw_top_border(painter, cursor_rect);
        }
    }
}


static void draw_pattern(PatternEditor & self) {
    doc::Document const & document = self.get_document();
    auto & visual = get_app().options().visual;

    self._image.fill(visual.overall_bg);

    {
        auto painter = QPainter(&self._image);

        GridRect canvas_rect = self._image.rect();

        ColumnLayout columns = gen_column_layout(self, document);

        // TODO build an abstraction for this
        {
            PainterScope scope{painter};

            GridRect outer_rect = canvas_rect;
            outer_rect.set_bottom(header::HEIGHT);
            painter.setClipRect(outer_rect);

            draw_header(self, document, columns, painter, outer_rect.size());
        }

        {
            PainterScope scope{painter};

            // Pattern body, relative to entire widget.
            GridRect absolute_rect = canvas_rect;
            absolute_rect.set_top(header::HEIGHT);
            painter.setClipRect(absolute_rect);

            // translate(offset) = the given offset is added to points.
            painter.translate(absolute_rect.left_top());

            // Pattern body size.
            QSize inner_size = absolute_rect.size();

            // First draw the row background. It lies in a regular grid.

            // TODO Is it possible to only redraw `rect`?
            // By setting the clip region, or skipping certain channels?

            // TODO When does Qt redraw a small rect? On non-compositing desktops?
            // On non-compositing KDE, Qt doesn't redraw when dragging a window on top.
            draw_pattern_background(self, document, columns, painter, inner_size);

            // Then for each channel, draw all notes in that channel lying within view.
            // Notes may be positioned at fractional beats that do not lie in the grid.
            draw_pattern_foreground(self, document, columns, painter, inner_size);
        }
    }

    {
        // Draw pixmap onto this widget.
        auto paint_on_screen = QPainter(&self);
        paint_on_screen.drawImage(self.rect(), self._image);
    }
}

void PatternEditor::paintEvent(QPaintEvent * /*event*/) {
    // Repaints the whole window, not just the invalidated area.
    // I've never seen event->rect() being anything other than the full widget.
    // Additionally, in Qt 5 Linux and Qt 6, event->rect() is expressed in virtual pixels,
    // which don't map 1:1 to a screen invalidation region in physical pixels,
    // making region-based invalidation nonsensical.

    // Is it practical to perform partial redraws when the canvas scrolls?
    // FamiTracker and BambooTracker(?) do it, but it's more difficult in Exo
    // since events can overlap.

    draw_pattern(*this);
}

// # Cursor movement

void PatternEditor::up_pressed(StateTransaction & tx) {
    doc::Document const & document = get_document();
    move_cursor::MoveCursorYArgs args{
        .rows_per_beat = _zoom_level,
        .step = _step,
    };
    auto const& move_cfg = get_app().options().move_cfg;

    auto cursor = get_cursor(*this);
    tx.cursor_mut().set_y(move_cursor::move_up(document, cursor, args, move_cfg));
}

void PatternEditor::down_pressed(StateTransaction & tx) {
    doc::Document const & document = get_document();
    move_cursor::MoveCursorYArgs args{
        .rows_per_beat = _zoom_level,
        .step = _step,
    };
    auto const& move_cfg = get_app().options().move_cfg;

    auto cursor = get_cursor(*this);
    tx.cursor_mut().set_y(move_cursor::move_down(document, cursor, args, move_cfg));
}


void PatternEditor::up_row_pressed(StateTransaction & tx) {
    doc::Document const & document = get_document();
    move_cursor::MoveCursorYArgs args{
        .rows_per_beat = _zoom_level,
        .step = 1,
    };
    auto const& move_cfg = get_app().options().move_cfg;

    auto cursor = get_cursor(*this);
    tx.cursor_mut().set_y(move_cursor::move_up(document, cursor, args, move_cfg));
}

void PatternEditor::down_row_pressed(StateTransaction & tx) {
    doc::Document const & document = get_document();
    move_cursor::MoveCursorYArgs args{
        .rows_per_beat = _zoom_level,
        .step = 1,
    };
    auto const& move_cfg = get_app().options().move_cfg;

    auto cursor = get_cursor(*this);
    tx.cursor_mut().set_y(move_cursor::move_down(document, cursor, args, move_cfg));
}


void PatternEditor::prev_beat_pressed(StateTransaction & tx) {
    doc::Document const & document = get_document();
    auto const & move_cfg = get_app().options().move_cfg;

    auto cursor_y = get_cursor(*this).y;
    tx.cursor_mut().set_y(move_cursor::prev_beat(document, cursor_y, move_cfg));
}

void PatternEditor::next_beat_pressed(StateTransaction & tx) {
    doc::Document const & document = get_document();
    auto const & move_cfg = get_app().options().move_cfg;

    auto cursor_y = get_cursor(*this).y;
    tx.cursor_mut().set_y(move_cursor::next_beat(document, cursor_y, move_cfg));
}


void PatternEditor::prev_event_pressed(StateTransaction & tx) {
    doc::Document const & document = get_document();
    auto ev_time = move_cursor::prev_event(document, get_cursor(*this));
    tx.cursor_mut().set_y(ev_time);
}

void PatternEditor::next_event_pressed(StateTransaction & tx) {
    doc::Document const & document = get_document();
    auto ev_time = move_cursor::next_event(document, get_cursor(*this));
    tx.cursor_mut().set_y(ev_time);
}


/// To avoid an infinite loop,
/// avoid scrolling more than _ patterns in a single Page Down keystroke.
constexpr int MAX_PAGEDOWN_SCROLL = 16;

void PatternEditor::scroll_prev_pressed(StateTransaction & tx) {
    doc::Document const & document = get_document();
    auto const & move_cfg = get_app().options().move_cfg;

    auto cursor_y = get_cursor(*this).y;

    cursor_y.beat -= move_cfg.page_down_distance;

    for (int i = 0; i < MAX_PAGEDOWN_SCROLL; i++) {
        if (cursor_y.beat < 0) {
            decrement_mod(
                cursor_y.grid, (GridIndex) document.timeline.size()
            );
            cursor_y.beat += document.timeline[cursor_y.grid].nbeats;
        } else {
            break;
        }
    }

    tx.cursor_mut().set_y(cursor_y);
}

void PatternEditor::scroll_next_pressed(StateTransaction & tx) {
    doc::Document const & document = get_document();
    auto const & move_cfg = get_app().options().move_cfg;

    auto cursor_y = get_cursor(*this).y;

    cursor_y.beat += move_cfg.page_down_distance;

    for (int i = 0; i < MAX_PAGEDOWN_SCROLL; i++) {
        auto const & grid_cell = document.timeline[cursor_y.grid];
        if (cursor_y.beat >= grid_cell.nbeats) {
            cursor_y.beat -= grid_cell.nbeats;
            increment_mod(
                cursor_y.grid, (GridIndex) document.timeline.size()
            );
        } else {
            break;
        }
    }

    tx.cursor_mut().set_y(cursor_y);
}

void PatternEditor::top_pressed(StateTransaction & tx) {
    auto cursor_y = get_cursor(*this).y;

    if (get_app().options().move_cfg.home_end_switch_patterns && cursor_y.beat <= 0) {
        if (cursor_y.grid.v > 0) {
            cursor_y.grid--;
        }
    }

    cursor_y.beat = 0;
    tx.cursor_mut().set_y(cursor_y);
}

void PatternEditor::bottom_pressed(StateTransaction & tx) {
    doc::Document const& document = get_document();
    auto raw_select = get_raw_sel(*this);

    // TODO pick a way of handling edge cases.
    //  We should use the same method of moving the cursor to end of pattern,
    //  as switching patterns uses (switch_grid_index()).
    //  calc_bottom() is dependent on selection's cached rows_per_beat (limitation)
    //  but selects one pattern exactly (good).

    auto calc_bottom = [&] (GridAndBeat cursor_y) -> BeatFraction {
        BeatFraction bottom_padding{1, _zoom_level};

        /*
        If a selection is active and bottom_padding() == 0,
        the naive approach would place the cursor at the end of a pattern,
        which is undesired (you can place otherwise-unreachable notes,
        and pressing down has no visual change).

        One option is to place the cursor on the next pattern.
        But at the end of the document, there is no next pattern.

        I decided to skip selecting the bottom row of the pattern.
        This is a tradeoff. There is no perfect solution.
        */
        if (raw_select && raw_select->bottom_padding() > 0) {
            bottom_padding = raw_select->bottom_padding();
        }
        return document.timeline[cursor_y.grid].nbeats - bottom_padding;
    };

    auto cursor_y = get_cursor(*this).y;
    auto bottom_beat = calc_bottom(cursor_y);

    if (
        get_app().options().move_cfg.home_end_switch_patterns
        && cursor_y.beat >= bottom_beat
    ) {
        if (cursor_y.grid + 1 < document.timeline.size()) {
            cursor_y.grid++;
            bottom_beat = calc_bottom(cursor_y);
        }
    }

    cursor_y.beat = bottom_beat;
    tx.cursor_mut().set_y(cursor_y);
}

template<void alter_mod(GridIndex & x, GridIndex den)>
inline void switch_grid_index(PatternEditor & self, StateTransaction & tx) {
    doc::Document const & document = self.get_document();
    auto cursor_y = get_cursor(self).y;

    alter_mod(cursor_y.grid, (GridIndex) document.timeline.size());

    BeatFraction nbeats = document.timeline[cursor_y.grid].nbeats;

    // If cursor is out of bounds, move to last row in pattern.
    if (cursor_y.beat >= nbeats) {
        BeatFraction rows = nbeats * self._zoom_level;
        int prev_row = util::math::frac_prev(rows);
        cursor_y.beat = BeatFraction{prev_row, self._zoom_level};
    }

    tx.cursor_mut().set_y(cursor_y);
}

void PatternEditor::prev_pattern_pressed(StateTransaction & tx) {
    switch_grid_index<decrement_mod>(*this, tx);
}
void PatternEditor::next_pattern_pressed(StateTransaction & tx) {
    switch_grid_index<increment_mod>(*this, tx);
}

static ColumnIndex ncol(ColumnList const& cols) {
    return (ColumnIndex) cols.size();
}

static SubColumnIndex nsubcol(ColumnList const& cols, CursorX const& cursor_x) {
    return (SubColumnIndex) cols[cursor_x.column].subcolumns.size();
}
static CellIndex ncell(ColumnList const& cols, CursorX const& cursor_x) {
    return cols[cursor_x.column].subcolumns[cursor_x.subcolumn].ncell;
}

/*
I implemented inclusive horizontal cursor movement because it's more familiar to users,
and to eliminate the "past-the-end" edge case in code.

Vertical cursor movement acts like inclusive indexing,
but allows the user to switch to exclusive indexing
which is useful when snapping the cursor to a non-grid-aligned event.
*/

static CursorX move_left(PatternEditor const& self, CursorX cursor_x) {
    doc::Document const& document = self.get_document();
    ColumnList cols = gen_column_list(self, document);

    // there's got to be a better way to write this code...
    // an elegant abstraction i'm missing

    if (cursor_x.cell > 0) {
        cursor_x.cell--;
    } else {
        if (cursor_x.subcolumn > 0) {
            cursor_x.subcolumn--;
        } else {
            if (cursor_x.column > 0) {
                cursor_x.column--;
            } else {
                cursor_x.column = ncol(cols) - 1;
            }
            cursor_x.subcolumn = nsubcol(cols, cursor_x) - 1;
        }
        cursor_x.cell = ncell(cols, cursor_x) - 1;
    }

    return cursor_x;
}

static CursorX move_right(PatternEditor const& self, CursorX cursor_x) {
    doc::Document const& document = self.get_document();
    ColumnList cols = gen_column_list(self, document);

    cursor_x.cell++;

    if (cursor_x.cell >= ncell(cols, cursor_x)) {
        cursor_x.cell = 0;
        cursor_x.subcolumn++;

        if (cursor_x.subcolumn >= nsubcol(cols, cursor_x)) {
            cursor_x.subcolumn = 0;
            cursor_x.column++;

            if (cursor_x.column >= ncol(cols)) {
                cursor_x.column = 0;
            }
        }
    }

    return cursor_x;
}

void PatternEditor::left_pressed(StateTransaction & tx) {
    auto cursor_x = get_cursor(*this).x;
    cursor_x = move_left(*this, cursor_x);

    tx.cursor_mut().set_x(cursor_x);
}

void PatternEditor::right_pressed(StateTransaction & tx) {
    auto cursor_x = get_cursor(*this).x;
    cursor_x = move_right(*this, cursor_x);

    tx.cursor_mut().set_x(cursor_x);
}

// TODO implement comparison between subcolumn variants,
// so you can hide pan on some but not all channels

// TODO disable wrapping if move_cfg.wrap_cursor is false.
// X coordinate (nchan, 0) may/not be legal, idk yet.

[[nodiscard]] static
CursorX cursor_clamp_subcol(ColumnList const& cols, CursorX cursor_x) {
    auto num_subcol = nsubcol(cols, cursor_x);

    // All effect channels in a given document have the same number of characters.
    // If not, this code would be wrong for effect columns,
    // and we would have to edit `character` beyond merely clamping it.
    // If you moved from [char1, char2, digit1, digit2] to [char, digit1, digit2],
    // character=2 starts at digit1 and ends at digit2.

    if (cursor_x.subcolumn >= num_subcol) {
        cursor_x.subcolumn = num_subcol - 1;
        cursor_x.cell = ncell(cols, cursor_x) - 1;
    } else {
        cursor_x.cell = std::min(
            cursor_x.cell, ncell(cols, cursor_x) - 1
        );
    }

    return cursor_x;
}

void PatternEditor::scroll_left_pressed(StateTransaction & tx) {
    doc::Document const & document = get_document();
    ColumnList cols = gen_column_list(*this, document);

    CursorX cursor_x = get_cursor(*this).x;
    if (cursor_x.column > 0) {
        cursor_x.column--;
    } else {
        cursor_x.column = ncol(cols) - 1;
    }

    cursor_x = cursor_clamp_subcol(cols, cursor_x);

    tx.cursor_mut().set_x(cursor_x);
}

void PatternEditor::scroll_right_pressed(StateTransaction & tx) {
    doc::Document const & document = get_document();
    ColumnList cols = gen_column_list(*this, document);

    CursorX cursor_x = get_cursor(*this).x;

    cursor_x.column++;
    if (cursor_x.column >= ncol(cols)) {
        cursor_x.column = 0;
    }

    cursor_x = cursor_clamp_subcol(cols, cursor_x);

    tx.cursor_mut().set_x(cursor_x);
}

void PatternEditor::escape_pressed() {
    auto tx = _win.edit_unwrap();
    tx.cursor_mut().clear_select();
}

void PatternEditor::toggle_edit_pressed() {
    _edit_mode = !_edit_mode;

    // Set the "cursor moved" flag
    // to redraw the pattern editor with the new cursor color.
    // We technically didn't move the cursor,
    // but this approach is less complex than adding an "edit mode changed" flag.
    auto tx = _win.edit_unwrap();
    tx.cursor_mut();
}

// Begin document mutation

static Cursor step_down_only(PatternEditor const& self, Cursor cursor) {
    doc::Document const & document = self.get_document();
    move_cursor::CursorStepArgs args{
        .rows_per_beat = self._zoom_level,
        .step = self._step,
        .step_to_event = self._step_to_event,
    };
    auto const& move_cfg = get_app().options().move_cfg;

    cursor.y = move_cursor::cursor_step(document, cursor, args, move_cfg);

    return cursor;
}

static Cursor step_cursor(PatternEditor const& self) {
    doc::Document const & document = self.get_document();
    auto cursor = get_cursor(self);

    switch (self._step_direction) {
    case StepDirection::Down:
        return step_down_only(self, cursor);

    case StepDirection::RightDigits: {
        ColumnList const& cols = gen_column_list(self, document);
        SubColumnCells const subcol =
            cols[cursor.x.column].subcolumns[cursor.x.subcolumn];

        CellIndex next_cell = cursor.x.cell + 1;

        if (std::holds_alternative<SubColumn_::Effect>(subcol.type)) {
            if (next_cell == document.effect_name_chars) {
                cursor.x.cell = 0;
                return step_down_only(self, cursor);

            } else if (next_cell >= subcol.ncell) {
                cursor.x.cell = document.effect_name_chars;
                return step_down_only(self, cursor);

            } else {
                cursor.x.cell++;
                return cursor;
            }

        } else {
            if (next_cell >= subcol.ncell) {
                cursor.x.cell = 0;
                return step_down_only(self, cursor);
            } else {
                cursor.x.cell++;
                return cursor;
            }
        }
    }

    case StepDirection::RightEffect: {
        ColumnList const& cols = gen_column_list(self, document);
        SubColumnCells const subcol =
            cols[cursor.x.column].subcolumns[cursor.x.subcolumn];

        CellIndex next_cell = cursor.x.cell + 1;
        if (next_cell >= subcol.ncell) {
            cursor.x.cell = 0;
            return step_down_only(self, cursor);
        } else {
            cursor.x.cell++;
            return cursor;
        }
    }

    case StepDirection::Right:
        cursor.x = move_right(self, cursor.x);
        return cursor;

    default:
        throw std::invalid_argument(fmt::format(
            "Invalid _step_direction {} when calling step_cursor()",
            (int) self._step_direction
        ));
    }
}

namespace ed = edit::edit_pattern;
using doc::ChipIndex;
using doc::ChannelIndex;

static std::tuple<ChipIndex, ChannelIndex, SubColumnCells, CellIndex>
calc_cursor_x(PatternEditor const & self) {
    doc::Document const & document = self.get_document();
    auto cursor_x = get_cursor(self).x;

    Column column = gen_column_list(self, document)[cursor_x.column];
    SubColumnCells subcolumn = column.subcolumns[cursor_x.subcolumn];

    return {column.chip, column.channel, subcolumn, cursor_x.cell};
}

// TODO Is there a more reliable method for me to ensure that
// all mutations are ignored in edit mode?
// And all regular keypresses are interpreted purely as note previews
// (regardless of column)?
// Maybe in keyPressEvent(), if edit mode off,
// preview notes and don't call mutator methods.
// Problem is, delete_key_pressed() is *not* called through keyPressEvent(),
// but through QShortcut.

void PatternEditor::delete_key_pressed() {
    if (!_edit_mode) {
        return;
    }
    doc::Document const & document = get_document();
    auto abs_time = get_cursor(*this).y;

    auto [chip, channel, subcolumn, _cell] = calc_cursor_x(*this);
    auto tx = _win.edit_unwrap();
    tx.push_edit(
        ed::delete_cell(document, chip, channel, subcolumn.type, abs_time),
        main_window::move_to(step_down_only(*this, get_cursor(*this)))
    );
}

static void note_pressed(
    PatternEditor & self, ChipIndex chip, ChannelIndex channel, doc::Note note
) {
    std::optional<doc::InstrumentIndex> instrument{};
    auto const& state = self._win._state;
    if (state._insert_instrument) {
        instrument = {state.instrument()};
    }

    auto abs_time = get_cursor(self).y;

    auto tx = self._win.edit_unwrap();
    tx.push_edit(
        ed::insert_note(
            self.get_document(), chip, channel, abs_time, note, instrument
        ),
        main_window::move_to(step_cursor(self))
    );
}

void PatternEditor::note_cut_pressed() {
    if (!_edit_mode) {
        return;
    }

    auto [chip, channel, subcolumn, _cell] = calc_cursor_x(*this);
    auto subp = &subcolumn.type;

    if (std::get_if<SubColumn_::Note>(subp)) {
        note_pressed(*this, chip, channel, doc::NOTE_CUT);
    }
}

void PatternEditor::select_all_pressed() {
    doc::Document const& document = get_document();

    ColumnList column_list = gen_column_list(*this, document);

    std::vector<cursor::SubColumnIndex> col_to_nsubcol;
    col_to_nsubcol.reserve(column_list.size());
    for (auto & col : column_list) {
        col_to_nsubcol.push_back(cursor::SubColumnIndex(col.subcolumns.size()));
    }

    // TODO add a method abstraction?
    auto tx = _win.edit_unwrap();
    auto & cursor = tx.cursor_mut();
    cursor.enable_select(_zoom_level);
    cursor.raw_select_mut()->select_all(document, col_to_nsubcol, _zoom_level);
}

void PatternEditor::selection_padding_pressed() {
    auto tx = _win.edit_unwrap();
    auto & cursor = tx.cursor_mut();
    if (auto & select = cursor.raw_select_mut()) {
        // If selection enabled, toggle whether to include bottom row.
        select->toggle_padding(_zoom_level);
    } else {
        // Otherwise create a single-cell selection.
        cursor.enable_select(_zoom_level);
    }
}

using edit::edit_pattern::MultiDigitField;

struct DigitField {
    /// Subset of SubColumn fields, only those with numeric values.
    MultiDigitField type;

    /// Number of numeric digits (excluding effect name).
    DigitIndex ndigit;
};

static void add_digit(
    PatternEditor & self,
    ChipIndex chip,
    ChannelIndex channel,
    DigitField field,
    DigitIndex digit_index,
    uint8_t nybble
) {
    using ed::DigitAction;
    using main_window::MoveCursor;

    auto const& document = self.get_document();
    auto abs_time = get_cursor(self).y;

    // TODO add support for DigitAction::ShiftLeft?
    // We'd have to track "cursor items" and "digits per item" separately,
    // and use ShiftLeft upon 1 item with 2 digits.

    DigitAction digit_action = field.ndigit <= 1
        // Single-digit subcolumns can be overwritten directly.
        ? DigitAction::Replace
        : digit_index == 0
            // Left digit is the 0xf0 nybble.
            ? DigitAction::UpperNybble
            // Right digit is the 0x0f nybble.
            : DigitAction::LowerNybble;

    // TODO add cursor movement modes
    MoveCursor move_cursor =  main_window::move_to(step_cursor(self));

    auto [number, box] = ed::add_digit(
        document, chip, channel, abs_time, field.type, digit_action, nybble
    );

    auto tx = self._win.edit_unwrap();
    tx.push_edit(std::move(box), move_cursor);

    // Update saved instrument number.
    if (std::holds_alternative<SubColumn_::Instrument>(field.type)) {
        // TODO if doc::MAX_INSTRUMENTS is reduced below 0x100,
        // we need to either clamp instrument numbers in the pattern data to MAX_INSTRUMENTS - 1,
        // or when setting the current instrument number,
        // or when the instrument dialog fetches the current instrument.
        tx.set_instrument(number);
    }

    // TODO update saved volume number? (is it useful?)
}

struct EffectField {
    SubColumn_::Effect type;
    CellIndex nchar;
};

static void add_effect_char(
    PatternEditor & self,
    ChipIndex chip,
    ChannelIndex channel,
    EffectField field,
    CellIndex char_index,
    char c)
{
    // TODO write a different function to insert an autocompleted effect atomically,
    // including two-character effects.

    using ed::EffectAction;
    namespace EffectAction_ = ed::EffectAction_;
    using main_window::MoveCursor;

    auto const& document = self.get_document();
    auto abs_time = get_cursor(self).y;

    doc::EffectName dummy_name{doc::EFFECT_NAME_PLACEHOLDER, c};

    auto effect_action = [&] () -> EffectAction {
        if (field.nchar <= 1) {
            assert(field.nchar == 1);
            // Single-character effect names can be overwritten directly.
            return EffectAction_::Replace(dummy_name.data());
        }

        assert(field.nchar == 2);
        if (char_index == 0) {
            return EffectAction_::LeftChar{c};
        } else {
            return EffectAction_::RightChar{c};
        }
    }();

    // TODO add cursor movement modes
    MoveCursor move_cursor =  main_window::move_to(step_cursor(self));

    auto box = ed::add_effect_char(
        document, chip, channel, abs_time, field.type, effect_action
    );
    auto tx = self._win.edit_unwrap();
    tx.push_edit(std::move(box), move_cursor);
}

/// Handles events based on physical layout rather than shortcuts.
/// Basically note and effect/hex input only.
void PatternEditor::keyPressEvent(QKeyEvent * event) {
    auto const& document = get_document();
    auto keycode = qkeycode::toKeycode(event);
    DEBUG_PRINT(
        "KeyPress {}=\"{}\", modifier {}, repeat? {}\n",
        keycode,
        qkeycode::KeycodeConverter::DomCodeToCodeString(keycode),
        event->modifiers(),
        event->isAutoRepeat()
    );

    auto [chip, channel, subcolumn, cell] = calc_cursor_x(*this);

    if (!_edit_mode) {
        // TODO preview note
        return;
    }

    auto subp = &subcolumn.type;

    if (std::get_if<SubColumn_::Note>(subp)) {
        // Pick the octave based on whether the user pressed the lower or upper key row.
        // If the user is holding shift, give the user an extra 2 octaves of range
        // (transpose the lower row down 1 octave, and the upper row up 1).
        bool shift_pressed = event->modifiers().testFlag(Qt::ShiftModifier);

        auto const & piano_keys = get_app().options().pattern_keys.piano_keys;

        for (auto const & [key_octave, key_row] : enumerate<int>(piano_keys)) {
            int octave;
            if (is_noise(document, chip, channel)) {
                // For noise channels, ignore global _octave, only use keyboard row.
                octave = key_octave;
            } else if (shift_pressed) {
                octave = _octave + key_octave + (key_octave > 0 ? 1 : -1);
            } else {
                octave = _octave + key_octave;
            }

            for (auto const [semitone, curr_key] : enumerate<int>(key_row)) {
                if (curr_key == keycode) {
                    int chromatic = octave * doc::NOTES_PER_OCTAVE + semitone;
                    chromatic =
                        std::clamp(chromatic, 0, (int) doc::CHROMATIC_COUNT - 1);

                    auto note = doc::Note{doc::NoteInt(chromatic)};
                    note_pressed(*this, chip, channel, note);
                    update();
                    return;
                }
            }
        }

    } else
    if (auto p = std::get_if<SubColumn_::Instrument>(subp)) {
        DigitField field{*p, (DigitIndex) subcolumn.ncell};
        if (auto nybble = format::hex_from_key(*event)) {
            add_digit(*this, chip, channel, field, (DigitIndex) cell, *nybble);
            update();
        }
    } else
    if (auto p = std::get_if<SubColumn_::Volume>(subp)) {
        DigitField field{*p, (DigitIndex) subcolumn.ncell};
        if (auto nybble = format::hex_from_key(*event)) {
            add_digit(*this, chip, channel, field, (DigitIndex) cell, *nybble);
            update();
        }
    } else
    if (auto p = std::get_if<SubColumn_::Effect>(subp)) {

        CellIndex digit_0_cell = document.effect_name_chars;
        if (cell >= digit_0_cell) {
            DigitField field{*p, 2};
            DigitIndex digit = cell - digit_0_cell;

            if (auto nybble = format::hex_from_key(*event)) {
                add_digit(*this, chip, channel, field, digit, *nybble);
                update();
            }
        } else {
            EffectField field{*p, document.effect_name_chars};
            if (auto c = format::alphanum_from_key(*event)) {
                add_effect_char(*this, chip, channel, field, cell, *c);
            }
        }
    } else
        throw std::logic_error("Invalid subcolumn passed to keyPressEvent()");
}

void PatternEditor::keyReleaseEvent(QKeyEvent * event) {
    auto dom_code = qkeycode::toKeycode(event);
    DEBUG_PRINT(
        "KeyRelease {}=\"{}\", modifier {}, repeat? {}\n",
        dom_code,
        qkeycode::KeycodeConverter::DomCodeToCodeString(dom_code),
        event->modifiers(),
        event->isAutoRepeat()
    );
    Q_UNUSED(dom_code)

    Super::keyReleaseEvent(event);
}

// namespace
}
