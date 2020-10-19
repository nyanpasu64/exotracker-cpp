#define PatternEditorPanel_INTERNAL public
#include "pattern_editor_panel.h"

#include "gui/lib/format.h"
#include "gui/lib/painter_ext.h"
#include "gui/cursor.h"
#include "gui/move_cursor.h"
#include "gui_common.h"
#include "chip_kinds.h"
#include "edit/edit_pattern.h"
#include "util/enumerate.h"
#include "util/math.h"
#include "util/release_assert.h"
#include "util/reverse.h"

#include <fmt/core.h>
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
using gui::lib::color::lerp_srgb;

using gui::lib::format::format_hex_1;
using gui::lib::format::format_hex_2;
namespace format = gui::lib::format;

using namespace gui::lib::painter_ext;

using util::math::increment_mod;
using util::math::decrement_mod;
using util::reverse::reverse;

using timing::MaybeSequencerTime;

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

W_OBJECT_IMPL(PatternEditorPanel)

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
    constexpr int EXTRA_WIDTH_DIVISOR = 3;

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
static void setup_shortcuts(PatternEditorPanel & self) {
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

    // Keystroke handlers have no arguments and don't know if Shift is held or not.
    using Method = void (PatternEditorPanel::*)();

    enum class AlterSelection {
        None,
        Clear,
        Extend,
    };

    // This code is confusing. Hopefully I can fix it.
    static auto const on_key_pressed = [] (
        PatternEditorPanel & self, Method method, AlterSelection alter_selection
    ) {
        if (alter_selection == AlterSelection::Clear) {
            self._win._cursor.clear_select();
        }
        if (alter_selection == AlterSelection::Extend) {
            // Begin or extend selection at old cursor position.
            self._win._cursor.enable_select(self._zoom_level);
        }
        // Move cursor.
        std::invoke(method, self);

        // This call is almost unnecessary.
        // Document edits and cursor motions already trigger redraws.
        // The only operation that doesn't is toggling edit mode, so keep this call.
        // Maybe edit mode will eventually be stored in the main window,
        // so you can toggle it from the toolbar.
        self.update();
    };

    // Connect cursor-movement keys to cursor-movement functions
    // (with/without shift held).
    auto connect_shortcut_pair = [&] (ShortcutPair & pair, Method method) {
        // Connect arrow keys to "clear selection and move cursor".
        QObject::connect(
            &pair.key,
            &QShortcut::activated,
            &self,
            [&self, method] () {
                on_key_pressed(self, method, AlterSelection::Clear);
            }
        );

        // Connect shift+arrow to "enable selection and move cursor".
        QObject::connect(
            &pair.shift_key,
            &QShortcut::activated,
            &self,
            [&self, method] () {
                on_key_pressed(self, method, AlterSelection::Extend);
            }
        );
    };

    // Copy, don't borrow, local lambdas.
    #define X(KEY) \
        connect_shortcut_pair(self._shortcuts.KEY, &PatternEditorPanel::KEY##_pressed);
    SHORTCUT_PAIRS(X, )
    #undef X

    auto connect_shortcut = [&] (QShortcut & shortcut, Method method) {
        QObject::connect(
            &shortcut,
            &QShortcut::activated,
            &self,
            [&self, method] () {
                on_key_pressed(self, method, AlterSelection::None);
            }
        );
    };

    #define X(KEY) \
        connect_shortcut(self._shortcuts.KEY, &PatternEditorPanel::KEY##_pressed);
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

    // Only width used so far. Instead of ascent/descent, we look at _pixels_per_row.
    return PatternFontMetrics{
        .width=width + visual.font_tweaks.width_adjust,
        .ascent=metrics.ascent(),
        .descent=metrics.descent()
    };
}

static void calc_font_metrics(PatternEditorPanel & self) {
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

void create_image(PatternEditorPanel & self) {
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

PatternEditorPanel::PatternEditorPanel(MainWindow * win, QWidget * parent)
    : QWidget(parent)
    , _win{*win}
    , _dummy_history{doc::DocumentCopy{}}
    , _history{_dummy_history}
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

void PatternEditorPanel::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);

    create_image(*this);
    // Qt automatically calls paintEvent().
}

// # Column layout
// See doc.h for documentation of how patterns work.

struct ChannelDraw {
    chip_common::ChipIndex chip;
    chip_common::ChannelIndex channel;
    int xleft;
    int xright;
};

namespace subcolumns = edit::edit_pattern::subcolumns;
using edit::edit_pattern::SubColumn;

// # Visual layout.

/// One column that the cursor can move into.
struct SubColumnPx {
    SubColumn type;

    // Determines the boundaries for click/selection handling.
    int left_px = 0;
    int right_px = 0;

    // Center for text rendering.
    qreal center_px = 0.0;

    SubColumnPx(SubColumn type) : type{type} {}
};

using SubColumnLayout = std::vector<SubColumnPx>;

struct LeftOfScreen{};
struct RightOfScreen{};

struct ColumnPx {
    chip_common::ChipIndex chip;
    chip_common::ChannelIndex channel;
    int left_px;
    int right_px;
    SubColumnPx block_handle;
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
    SubColumnPx ruler;
    std::vector<MaybeColumnPx> cols;
};

/// Compute where on-screen to draw each pattern column.
[[nodiscard]] static ColumnLayout gen_column_layout(
    PatternEditorPanel const & self,
    doc::Document const & document
) {
    int const width_per_char = self._pattern_font_metrics.width;
    int const extra_width = width_per_char / columns::EXTRA_WIDTH_DIVISOR;

    int x_px = 0;

    auto add_padding = [&x_px, extra_width] () {
        x_px += extra_width;
    };

    auto begin_sub = [&x_px, add_padding] (SubColumnPx & sub, bool pad = true) {
        sub.left_px = x_px;
        if (pad) {
            add_padding();
        }
    };

    auto center_sub = [&x_px, width_per_char] (
        SubColumnPx & sub, int nchar_num, int nchar_den = 1
    ) {
        int dwidth = int(width_per_char * nchar_num / nchar_den);
        sub.center_px = x_px + dwidth / qreal(2.0);
        x_px += dwidth;
    };

    auto end_sub = [&x_px, add_padding] (SubColumnPx & sub, bool pad = true) {
        if (pad) {
            add_padding();
        }
        sub.right_px = x_px;
    };

    // SubColumn doesn't matter.
    SubColumnPx ruler{subcolumns::Note{}};

    begin_sub(ruler);
    center_sub(ruler, columns::RULER_WIDTH_CHARS);
    end_sub(ruler);

    ColumnLayout column_layout{.ruler = ruler, .cols = {}};

    for (
        chip_common::ChipIndex chip_index = 0;
        chip_index < document.chips.size();
        chip_index++
    ) {
        for (
            chip_common::ChannelIndex channel_index = 0;
            channel_index < document.chip_index_to_nchan(chip_index);
            channel_index++
        ) {
            int const orig_left_px = x_px;

            // SubColumn doesn't matter.
            SubColumnPx block_handle{subcolumns::Note{}};

            begin_sub(block_handle);
            center_sub(block_handle, 1, 2);
            end_sub(block_handle);

            SubColumnLayout subcolumns;
            // TODO change doc to list how many effect colums there are

            auto append_subcolumn = [&subcolumns, begin_sub, center_sub, end_sub] (
                SubColumn type,
                int nchar,
                bool pad_left = true,
                bool pad_right = true
            ) {
                SubColumnPx sub{type};

                begin_sub(sub, pad_left);
                center_sub(sub, nchar);
                end_sub(sub, pad_right);

                subcolumns.push_back(sub);
            };

            // Notes are 3 characters wide.
            append_subcolumn(subcolumns::Note{}, 3);

            // TODO configurable column hiding (one checkbox per column type?)
            // Instruments are 2 characters wide.
            append_subcolumn(subcolumns::Instrument{}, 2);

            // TODO Document::get_volume_width(chip_index, chan_index)
            // Volumes are 2 characters wide.
            append_subcolumn(subcolumns::Volume{}, 2);

            for (uint8_t effect_col = 0; effect_col < 1; effect_col++) {
                // Effect names are 2 characters wide and only have left padding.
                append_subcolumn(
                    subcolumns::EffectName{effect_col}, 2, true, false
                );
                // Effect values are 2 characters wide and only have right padding.
                append_subcolumn(
                    subcolumns::EffectValue{effect_col}, 2, false, true
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

using SubColumnList = std::vector<SubColumn>;

struct Column {
    chip_common::ChipIndex chip;
    chip_common::ChannelIndex channel;
    SubColumnList subcolumns;
};

using ColumnList = std::vector<Column>;

/// Generates order of all sub/columns // (not just visible columns)
/// for keyboard-based movement rather than rendering.
///
/// TODO add function in self for determining subcolumn visibility.
[[nodiscard]] static ColumnList gen_column_list(
    PatternEditorPanel const & self,
    doc::Document const & document
) {
    ColumnList column_list;

    for (
        chip_common::ChipIndex chip_index = 0;
        chip_index < document.chips.size();
        chip_index++
    ) {
        for (
            chip_common::ChannelIndex channel_index = 0;
            channel_index < document.chip_index_to_nchan(chip_index);
            channel_index++
        ) {
            SubColumnList subcolumns;
            // TODO change doc to list how many effect colums there are

            // Notes are 3 characters wide.
            subcolumns.push_back(subcolumns::Note{});

            // TODO configurable column hiding (one checkbox per column type?)
            // Instruments are 2 characters wide.
            subcolumns.push_back(subcolumns::Instrument{});

            // TODO Document::get_volume_width(chip_index, chan_index)
            // Volumes are 2 characters wide.
            subcolumns.push_back(subcolumns::Volume{});

            for (uint8_t effect_col = 0; effect_col < 1; effect_col++) {
                subcolumns.push_back(subcolumns::EffectName{effect_col});
                subcolumns.push_back(subcolumns::EffectValue{effect_col});
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
    PatternEditorPanel & self,
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
        channel_rect.set_left(columns.ruler.left_px);
        channel_rect.set_right(columns.ruler.right_px);

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
            QString("chip %1 chan %2").arg(chip).arg(channel)
        );

        draw_header_border(channel_rect);
    }
}


namespace {

// yay inconsistency
using PxInt = int;
//using PxNat = uint32_t;

/// Convert a relative timestamp to a vertical display offset.
PxInt pixels_from_beat(PatternEditorPanel const & widget, BeatFraction beat) {
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
    PatternEditorPanel const & _widget;
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
        PatternEditorPanel const & widget,  // holds reference
        doc::Document const & document,  // holds reference
        PxInt const screen_height
    ) {
        PxInt const cursor_from_pattern_top =
            pixels_from_beat(widget, widget._win._cursor->y.beat);

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
            scroll_position = widget._win._cursor->y;

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

QLinearGradient make_gradient(
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

using cursor::CursorX;
using cursor::ColumnIndex;
using cursor::SubColumnIndex;

using CellIter = doc::TimelineCellIterRef;

/// Computing colors may require blending with the background color.
/// So cache the color for each timeline entry being drawn.
#define CACHE_COLOR(COLOR) \
    QColor COLOR = visual.COLOR(pos.focused);

/// Draw the background lying behind notes/etc.
static void draw_pattern_background(
    PatternEditorPanel & self,
    doc::Document const &document,
    ColumnLayout const & columns,
    QPainter & painter,
    QSize const inner_size
) {
    auto & visual = get_app().options().visual;

    int row_right_px = columns.ruler.right_px;
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
                    QPoint{sub.left_px, pos.top}, QPoint{sub.right_px, pos.bottom}
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

                namespace sc = subcolumns;

                // Don't draw the note column's divider line,
                // since it lies right next to the previous channel's channel divider.
                CASE(sc::Note, note_bg, note_divider)
                CASE(sc::Instrument, instrument_bg, instrument_divider)
                CASE(sc::Volume, volume_bg, volume_divider)
                CASE(sc::EffectName, effect_bg, effect_divider)
                CASE_NO_FG(sc::EffectValue, effect_bg)

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

    draw_divider(columns.ruler.right_px);
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
            QPoint{sub.left_px, 0},
            QPoint{sub.right_px + painter.pen().width(), pos.bottom - pos.top}
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

            qreal x0 = sub.left_px + 1;
            qreal x1 = sub.right_px;
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
    if (auto maybe_select = self._win._cursor.get_select()) {
        auto select = *maybe_select;

        // Limit selections to patterns, not ruler.
        PainterScope scope{painter};
        painter.setClipRect(GridRect::from_corners(
            columns.ruler.right_px, 0, inner_size.width(), inner_size.height()
        ));

        int off_screen = std::max(inner_size.width(), inner_size.height()) + 100;

        using MaybePxInt = std::optional<PxInt>;

        /// Overwritten with the estimated top/bottom of the selection on-screen.
        /// Set to INT_MIN/MAX if selection endpoint is above or below screen.
        /// Only uset if 0 patterns were visited.
        MaybePxInt maybe_select_top{};
        MaybePxInt maybe_select_bottom{};

        /// Every time we compare an endpoint against a pattern,
        /// we can identify if it's within, above, or below.
        ///
        /// If within, overwrite position unconditionally.
        /// If above/below, overwrite position if not present.
        ///
        /// If the endpoint is within *any* pattern, we write the exact position.
        /// Otherwise we identify whether it's above or below the screen.
        auto find_selection = [&] (GridCellPosition const & pos) {
            using Frac = BeatFraction;

            auto calc_row = [&] (GridAndBeat y, std::optional<PxInt> & select_pos) {
                // If row is in pattern, return exact position.
                if (y.grid == pos.grid) {
                    Frac row = y.beat * self._zoom_level;
                    PxInt yPx = doc::round_to_int(self._pixels_per_row * row);

                    select_pos = pos.top + yPx;
                    return;
                }

                // If row is at end of pattern, return exact position.
                // Because if cursor is "past end of document",
                // it isn't owned by any pattern, but needs to be positioned correctly.
                if (y.grid.v == pos.grid + 1 && y.beat == 0) {
                    select_pos = pos.bottom;
                    return;
                }

                // If row is above screen, set to top of universe (if no value present).
                if (y.grid < pos.grid) {
                    select_pos = select_pos.value_or(-off_screen);
                    return;
                }

                // If row is below screen, set to bottom of universe (if no value present).
                release_assert(y.grid > pos.grid);
                select_pos = select_pos.value_or(+off_screen);
            };

            calc_row(select.top, maybe_select_top);
            calc_row(select.bottom, maybe_select_bottom);
        };

        foreach_grid(find_selection);

        // It should be impossible to position the cursor such that 0 patterns are drawn.
        if (!(maybe_select_top && maybe_select_bottom)) {
            throw std::logic_error("Trying to draw selection with 0 patterns");
        }

        PxInt & select_top = *maybe_select_top;
        PxInt & select_bottom = *maybe_select_bottom;

        release_assert(select_top <= select_bottom);

        auto get_select_x = [&] (CursorX x, bool right_border) {
            auto const& c = columns.cols[x.column];
            if (c.has_value()) {
                SubColumnPx sc = c->subcolumns[x.subcolumn];
                return right_border ? sc.right_px : sc.left_px;
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

        PxInt select_left = get_select_x(select.left, false);
        PxInt select_right = get_select_x(select.right, true);

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

        auto cursor_x = self._win._cursor->x;
        if (cursor_x.column >= columns.cols.size()) {
            cursor_x.column = 0;
            cursor_x.subcolumn = 0;
        }

        // Draw background for cursor row and cell.
        if (auto & col = columns.cols[cursor_x.column]) {
            // If cursor is on-screen, draw left/cursor/right.
            auto subcol = col->subcolumns[cursor_x.subcolumn];

            // Draw gradient (space to the left of the cursor cell).
            auto left_rect = cursor_row_rect;
            left_rect.set_right(subcol.left_px);
            painter.fillRect(left_rect, bg_grad);

            // Draw gradient (space to the right of the cursor cell).
            auto right_rect = cursor_row_rect;
            right_rect.set_left(subcol.right_px);
            painter.fillRect(right_rect, bg_grad);

            // Draw gradient (cursor cell only).
            GridRect cursor_rect{
                QPoint{subcol.left_px, cursor_top},
                QPoint{subcol.right_px, cursor_bottom}
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
                    columns.ruler.center_px,
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
    PatternEditorPanel & self,
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
        qreal x1f = subcolumn.center_px - rect_width / 2;
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
        qreal x1f = subcolumn.center_px - rect_width / 2;
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
        // CACHE_COLOR(effect); (not needed yet)

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
            doc::TimeInPattern time = timed_event.time;
            doc::RowEvent row_event = timed_event.v;

            // Compute where to draw row.
            Frac beat = time.anchor_beat;
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
                SubColumnPx sub, int left_offset = 0
            ) {
                QPoint left_top{sub.left_px + left_offset, 0};
                QPoint right_top{sub.right_px, 0};

                // Draw top border. Do it after each note clears the background.
                painter.setPen(note_color);
                draw_top_border(painter, left_top, right_top);
            };

            // Draw text.
            for (auto const & subcolumn : column.subcolumns) {
                namespace sc = subcolumns;

                auto draw_text = [&](QString & text) {
                    // Clear background using unmodified copy free of rendered text.
                    // Unlike alpha transparency, this doesn't break ClearType
                    // and may be faster as well.
                    // Multiply by 1.5 or 2-ish if character tails are not being cleared.
                    auto clear_height = self._pixels_per_row;

                    GridRect target_rect{
                        QPoint{subcolumn.left_px, 0},
                        QPoint{subcolumn.right_px, clear_height},
                    };
                    auto sample_rect = painter.combinedTransform().mapRect(target_rect);
                    painter.drawImage(target_rect, self._temp_image.copy(sample_rect));

                    // Text is being drawn relative to top-left of current row (not cell).
                    // subcolumn.center_px is relative to screen left (not cell).
                    text_painter.draw_text(
                        painter,
                        subcolumn.center_px,
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

                        if (note.is_cut()) {
                            draw_note_cut(subcolumn, note_color);
                        } else if (note.is_release()) {
                            draw_release(subcolumn, note_color);
                        } else {
                            painter.setPen(note_color);
                            QString s = format::midi_to_note_name(
                                note_cfg, document.accidental_mode, note
                            );
                            draw_text(s);
                        }

                        draw_top_line(subcolumn, painter.pen().width());
                    }
                }
                CASE(sc::Instrument) {
                    if (row_event.instr) {
                        painter.setPen(instrument);
                        auto s = format_hex_2(uint8_t(*row_event.instr));
                        draw_text(s);

                        draw_top_line(subcolumn);
                    }
                }

                CASE(sc::Volume) {
                    if (row_event.volume) {
                        painter.setPen(volume);
                        auto s = format_hex_2(uint8_t(*row_event.volume));
                        draw_text(s);

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

        int row_right_px = columns.ruler.right_px;
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
        auto cursor_x = self._win._cursor->x;

        // If cursor is on-screen, draw cell outline.
        if (auto & col = columns.cols[cursor_x.column]) {
            auto subcol = col->subcolumns[cursor_x.subcolumn];
            GridRect cursor_rect{
                QPoint{subcol.left_px, cursor_top},
                QPoint{subcol.right_px, cursor_bottom}
            };

            // Draw top line.
            painter.setPen(visual.cell);
            draw_top_border(painter, cursor_rect);
        }
    }
}


static void draw_pattern(PatternEditorPanel & self, const QRect repaint_rect) {
    doc::Document const & document = self.get_document();
    auto & visual = get_app().options().visual;

    // TODO maybe only draw repaint_rect? And use Qt::IntersectClip?

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
        paint_on_screen.drawImage(repaint_rect, self._image);
    }
}

void PatternEditorPanel::paintEvent(QPaintEvent *event) {
    draw_pattern(*this, event->rect());
}

// # Cursor movement

void PatternEditorPanel::up_pressed() {
    doc::Document const & document = get_document();
    move_cursor::MoveCursorYArgs args{
        .rows_per_beat = _zoom_level,
        .step = _step,
        .step_to_event = _step_to_event,
    };
    auto const& move_cfg = get_app().options().move_cfg;

    auto cursor = _win._cursor.get();
    _win._cursor.set_y(move_cursor::move_up(document, cursor, args, move_cfg));
}

void PatternEditorPanel::down_pressed() {
    doc::Document const & document = get_document();
    move_cursor::MoveCursorYArgs args{
        .rows_per_beat = _zoom_level,
        .step = _step,
        .step_to_event = _step_to_event,
    };
    auto const& move_cfg = get_app().options().move_cfg;

    auto cursor = _win._cursor.get();
    _win._cursor.set_y(move_cursor::move_down(document, cursor, args, move_cfg));
}


void PatternEditorPanel::prev_beat_pressed() {
    doc::Document const & document = get_document();
    auto const & move_cfg = get_app().options().move_cfg;

    auto cursor_y = _win._cursor.get().y;
    _win._cursor.set_y(move_cursor::prev_beat(document, cursor_y, move_cfg));
}

void PatternEditorPanel::next_beat_pressed() {
    doc::Document const & document = get_document();
    auto const & move_cfg = get_app().options().move_cfg;

    auto cursor_y = _win._cursor.get().y;
    _win._cursor.set_y(move_cursor::next_beat(document, cursor_y, move_cfg));
}


void PatternEditorPanel::prev_event_pressed() {
    doc::Document const & document = get_document();
    auto ev_time = move_cursor::prev_event(document, _win._cursor.get());
    _win._cursor.set_y(ev_time);
}

void PatternEditorPanel::next_event_pressed() {
    doc::Document const & document = get_document();
    auto ev_time = move_cursor::next_event(document, _win._cursor.get());
    _win._cursor.set_y(ev_time);
}


/// To avoid an infinite loop,
/// avoid scrolling more than _ patterns in a single Page Down keystroke.
constexpr int MAX_PAGEDOWN_SCROLL = 16;

void PatternEditorPanel::scroll_prev_pressed() {
    doc::Document const & document = get_document();
    auto const & move_cfg = get_app().options().move_cfg;

    auto cursor_y = _win._cursor.get().y;

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

    _win._cursor.set_y(cursor_y);
}

void PatternEditorPanel::scroll_next_pressed() {
    doc::Document const & document = get_document();
    auto const & move_cfg = get_app().options().move_cfg;

    auto cursor_y = _win._cursor.get().y;

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

    _win._cursor.set_y(cursor_y);
}

void PatternEditorPanel::top_pressed() {
    auto cursor_y = _win._cursor.get().y;

    if (get_app().options().move_cfg.home_end_switch_patterns && cursor_y.beat <= 0) {
        if (cursor_y.grid.v > 0) {
            cursor_y.grid--;
        }
    }

    cursor_y.beat = 0;
    _win._cursor.set_y(cursor_y);
}

void PatternEditorPanel::bottom_pressed() {
    doc::Document const& document = get_document();
    auto raw_select = _win._cursor.raw_select();

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

    auto cursor_y = _win._cursor.get().y;
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
    _win._cursor.set_y(cursor_y);
}

template<void alter_mod(GridIndex & x, GridIndex den)>
inline void switch_grid_index(PatternEditorPanel & self) {
    doc::Document const & document = self.get_document();
    auto cursor_y = self._win._cursor.get().y;

    alter_mod(cursor_y.grid, (GridIndex) document.timeline.size());

    BeatFraction nbeats = document.timeline[cursor_y.grid].nbeats;

    // If cursor is out of bounds, move to last row in pattern.
    if (cursor_y.beat >= nbeats) {
        BeatFraction rows = nbeats * self._zoom_level;
        int prev_row = util::math::frac_prev(rows);
        cursor_y.beat = BeatFraction{prev_row, self._zoom_level};
    }

    self._win._cursor.set_y(cursor_y);
}

void PatternEditorPanel::prev_pattern_pressed() {
    switch_grid_index<decrement_mod>(*this);
}
void PatternEditorPanel::next_pattern_pressed() {
    switch_grid_index<increment_mod>(*this);
}

ColumnIndex ncol(ColumnList const& cols) {
    return ColumnIndex(cols.size());
}

SubColumnIndex nsubcol(ColumnList const& cols, size_t column_idx) {
    return SubColumnIndex(cols[column_idx].subcolumns.size());
}

/// Transforms a "past the end" cursor to point to the beginning instead.
/// Call this before moving the cursor towards the right.
void wrap_cursor(ColumnList const& cols, CursorX & cursor_x) {
    if (cursor_x.column >= ncol(cols)) {
        cursor_x.column = 0;
    }
}

/*
I implemented inclusive horizontal cursor movement because it's more familiar to users,
and to eliminate the "past-the-end" edge case in code.

Vertical cursor movement acts like inclusive indexing,
but allows the user to switch to exclusive indexing
which is useful when snapping the cursor to a non-grid-aligned event.
*/

void PatternEditorPanel::left_pressed() {
    doc::Document const & document = get_document();
    ColumnList cols = gen_column_list(*this, document);

    // there's got to be a better way to write this code...
    // an elegant abstraction i'm missing
    auto cursor_x = _win._cursor.get().x;

    if (cursor_x.subcolumn > 0) {
        cursor_x.subcolumn--;
    } else {
        if (cursor_x.column > 0) {
            cursor_x.column--;
        } else {
            cursor_x.column = ncol(cols) - 1;
        }
        cursor_x.subcolumn = nsubcol(cols, cursor_x.column) - 1;
    }

    _win._cursor.set_x(cursor_x);
}

void PatternEditorPanel::right_pressed() {
    doc::Document const & document = get_document();
    ColumnList cols = gen_column_list(*this, document);

    // Is it worth extracting cursor movement logic to a class?
    auto cursor_x = _win._cursor.get().x;
    wrap_cursor(cols, cursor_x);
    cursor_x.subcolumn++;

    if (cursor_x.subcolumn >= nsubcol(cols, cursor_x.column)) {
        cursor_x.subcolumn = 0;
        cursor_x.column++;

        if (cursor_x.column >= ncol(cols)) {
            cursor_x.column = 0;
        }
    }

    _win._cursor.set_x(cursor_x);
}

// TODO implement comparison between subcolumn variants,
// so you can hide pan on some but not all channels

// TODO disable wrapping if move_cfg.wrap_cursor is false.
// X coordinate (nchan, 0) may/not be legal, idk yet.

void PatternEditorPanel::scroll_left_pressed() {
    doc::Document const & document = get_document();
    ColumnList cols = gen_column_list(*this, document);

    auto cursor_x = _win._cursor.get().x;
    if (cursor_x.column > 0) {
        cursor_x.column--;
    } else {
        cursor_x.column = ncol(cols) - 1;
    }

    cursor_x.subcolumn =
        std::min(cursor_x.subcolumn, nsubcol(cols, cursor_x.column) - 1);

    _win._cursor.set_x(cursor_x);
}

void PatternEditorPanel::scroll_right_pressed() {
    doc::Document const & document = get_document();
    ColumnList cols = gen_column_list(*this, document);

    auto cursor_x = _win._cursor.get().x;
    cursor_x.column++;
    wrap_cursor(cols, cursor_x);
    cursor_x.subcolumn =
        std::min(cursor_x.subcolumn, nsubcol(cols, cursor_x.column) - 1);

    _win._cursor.set_x(cursor_x);
}

void PatternEditorPanel::escape_pressed() {
    _win._cursor.clear_select();
}

void PatternEditorPanel::toggle_edit_pressed() {
    _edit_mode = !_edit_mode;
}

// Begin document mutation

static cursor::Cursor step_cursor_down(PatternEditorPanel const& self) {
    doc::Document const & document = self.get_document();
    auto cursor = self._win._cursor.get();
    move_cursor::MoveCursorYArgs args{
        .rows_per_beat = self._zoom_level,
        .step = self._step,
        .step_to_event = self._step_to_event,
    };
    auto const& move_cfg = get_app().options().move_cfg;

    cursor.y = move_cursor::cursor_step(document, cursor, args, move_cfg);

    return cursor;
}

namespace ed = edit::edit_pattern;

auto calc_cursor_x(PatternEditorPanel const & self) ->
    std::tuple<doc::ChipIndex, doc::ChannelIndex, SubColumn>
{
    doc::Document const & document = self.get_document();
    auto cursor_x = self._win._cursor->x;

    Column column = gen_column_list(self, document)[cursor_x.column];
    SubColumn subcolumn = column.subcolumns[cursor_x.subcolumn];

    return {column.chip, column.channel, subcolumn};
}

// TODO Is there a more reliable method for me to ensure that
// all mutations are ignored in edit mode?
// And all regular keypresses are interpreted purely as note previews
// (regardless of column)?
// Maybe in keyPressEvent(), if edit mode off,
// preview notes and don't call mutator methods.
// Problem is, delete_key_pressed() is *not* called through keyPressEvent(),
// but through QShortcut.

void PatternEditorPanel::delete_key_pressed() {
    if (!_edit_mode) {
        return;
    }
    doc::Document const & document = get_document();
    auto abs_time = _win._cursor->y;

    auto [chip, channel, subcolumn] = calc_cursor_x(*this);
    _win.push_edit(
        ed::delete_cell(document, chip, channel, subcolumn, abs_time),
        main_window::move_to(step_cursor_down(*this))
    );
}

void note_pressed(
    PatternEditorPanel & self,
    doc::ChipIndex chip,
    doc::ChannelIndex channel,
    doc::Note note
) {
    std::optional<doc::InstrumentIndex> instrument{};
    if (self._win._insert_instrument) {
        instrument = {self._win._instrument};
    }

    auto abs_time = self._win._cursor->y;

    self._win.push_edit(
        ed::insert_note(
            self.get_document(), chip, channel, abs_time, note, instrument
        ),
        main_window::move_to(step_cursor_down(self))
    );
}

void PatternEditorPanel::note_cut_pressed() {
    if (!_edit_mode) {
        return;
    }

    auto [chip, channel, subcolumn] = calc_cursor_x(*this);
    auto subp = &subcolumn;

    if (std::get_if<subcolumns::Note>(subp)) {
        note_pressed(*this, chip, channel, doc::NOTE_CUT);
    }
}

void PatternEditorPanel::select_all_pressed() {
    doc::Document const& document = get_document();

    ColumnList column_list = gen_column_list(*this, document);

    std::vector<cursor::SubColumnIndex> col_to_nsubcol;
    col_to_nsubcol.reserve(column_list.size());
    for (auto & col : column_list) {
        col_to_nsubcol.push_back(cursor::SubColumnIndex(col.subcolumns.size()));
    }

    // TODO add a method abstraction?
    _win._cursor.enable_select(_zoom_level);
    _win._cursor.raw_select_mut()->select_all(document, col_to_nsubcol, _zoom_level);
}

void PatternEditorPanel::selection_padding_pressed() {
    if (auto & select = _win._cursor.raw_select_mut()) {
        // If selection enabled, toggle whether to include bottom row.
        select->toggle_padding(_zoom_level);
    } else {
        // Otherwise create a single-cell selection.
        _win._cursor.enable_select(_zoom_level);
    }
}

static void add_digit(
    PatternEditorPanel & self,
    doc::ChipIndex chip,
    doc::ChannelIndex channel,
    uint8_t nybble,
    ed::MultiDigitField field
) {
    auto const& document = self.get_document();
    auto abs_time = self._win._cursor->y;

    int digit_index = self._win._cursor.digit_index();
    auto [number, box] = ed::add_digit(
        document, chip, channel, abs_time, field, digit_index, nybble
    );

    if (digit_index == 0) {
        // Erase field and enter first digit.
        self._win.push_edit(std::move(box), main_window::MoveCursor_::AdvanceDigit{});

    } else {
        // Move current digit to the left, append second digit,
        // and move cursor down.
        self._win.push_edit(
            std::move(box), main_window::move_to(step_cursor_down(self))
        );
    }
    // Update saved instrument number.
    if (std::holds_alternative<subcolumns::Instrument>(field)) {
        self._win._instrument = number;
    }

    // TODO update saved volume number? (is it useful?)
}

static void add_instrument_digit(
    PatternEditorPanel & self,
    doc::ChipIndex chip,
    doc::ChannelIndex channel,
    uint8_t nybble
) {
    add_digit(self, chip, channel, nybble, subcolumns::Instrument{});
}

static void add_volume_digit(
    PatternEditorPanel & self,
    doc::ChipIndex chip,
    doc::ChannelIndex channel,
    uint8_t nybble
) {
    // TODO add support for single-digit volume?
    add_digit(self, chip, channel, nybble, subcolumns::Volume{});
}

/// Handles events based on physical layout rather than shortcuts.
/// Basically note and effect/hex input only.
void PatternEditorPanel::keyPressEvent(QKeyEvent * event) {
    auto keycode = qkeycode::toKeycode(event);
    DEBUG_PRINT(
        "KeyPress {}=\"{}\", modifier {}, repeat? {}\n",
        keycode,
        qkeycode::KeycodeConverter::DomCodeToCodeString(keycode),
        event->modifiers(),
        event->isAutoRepeat()
    );

    auto [chip, channel, subcolumn] = calc_cursor_x(*this);

    if (!_edit_mode) {
        // TODO preview note
        return;
    }

    auto subp = &subcolumn;

    if (std::get_if<subcolumns::Note>(subp)) {
        // Pick the octave based on whether the user pressed the lower or upper key row.
        // If the user is holding shift, give the user an extra 2 octaves of range
        // (transpose the lower row down 1 octave, and the upper row up 1).
        bool shift_pressed = event->modifiers().testFlag(Qt::ShiftModifier);

        auto const & piano_keys = get_app().options().pattern_keys.piano_keys;

        for (auto const & [key_octave, key_row] : enumerate<int>(piano_keys)) {
            int octave = _octave;
            if (shift_pressed) {
                octave += key_octave + (key_octave > 0 ? 1 : -1);
            } else {
                octave += key_octave;
            }

            for (auto const [semitone, curr_key] : enumerate<int>(key_row)) {
                int chromatic = octave * lib::format::NOTES_PER_OCTAVE + semitone;
                chromatic = std::clamp(chromatic, 0, doc::CHROMATIC_COUNT - 1);

                if (curr_key == keycode) {
                    auto note = doc::Note{doc::ChromaticInt(chromatic)};
                    note_pressed(*this, chip, channel, note);
                    update();
                    return;
                }
            }
        }

    } else
    if (std::get_if<subcolumns::Instrument>(subp)) {
        if (auto digit = format::hex_from_key(*event)) {
            add_instrument_digit(*this, chip, channel, *digit);
            update();
        }
    } else
    if (std::get_if<subcolumns::Volume>(subp)) {
        if (auto digit = format::hex_from_key(*event)) {
            add_volume_digit(*this, chip, channel, *digit);
            update();
        }
    }
}

void PatternEditorPanel::keyReleaseEvent(QKeyEvent * event) {
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
