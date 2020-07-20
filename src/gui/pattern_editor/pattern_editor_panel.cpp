#define PatternEditorPanel_INTERNAL public
#include "pattern_editor_panel.h"

#include "gui/lib/format.h"
#include "gui/lib/painter_ext.h"
#include "gui/cursor.h"
#include "gui/main_window.h"
#include "gui_common.h"
#include "chip_kinds.h"
#include "edit/pattern.h"
#include "util/compare.h"
#include "util/enumerate.h"
#include "util/math.h"
#include "util/reverse.h"

#include <fmt/core.h>
#include <verdigris/wobjectimpl.h>
#include <qkeycode/qkeycode.h>

#include <QApplication>
#include <QColor>
#include <QDebug>  // unused
#include <QFont>
#include <QFontMetrics>
#include <QGradient>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPainter>
#include <QPoint>
#include <QRect>

#include <algorithm>  // std::max, std::clamp
#include <cmath>  // round
#include <functional>  // std::invoke
#include <optional>
#include <tuple>
#include <type_traits>  // is_same_v
#include <variant>
#include <vector>

namespace gui::pattern_editor {

using gui::lib::color::lerp;
using gui::lib::color::lerp_colors;
using gui::lib::color::lerp_srgb;

using gui::lib::format::format_hex_1;
using gui::lib::format::format_hex_2;
namespace gui_fmt = gui::lib::format;

using namespace gui::lib::painter_ext;

using util::math::increment_mod;
using util::math::decrement_mod;
using util::math::frac_floor;
using util::math::frac_ceil;
using util::reverse::reverse;

using timing::MaybeSequencerTime;

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
- Follow audio thread's location (pattern/row), when audio thread is playing.
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

    auto & shortcut_keys = get_app().options().pattern_keys;

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

    // This code is confusing. Hopefully I can fix it.
    static auto const on_key_pressed = [] (
        PatternEditorPanel & self, Method method, bool clear_selection
    ) {
        // TODO encapsulate cursor, allow moving with mouse, show selection, etc.
        std::invoke(method, self);
        if (clear_selection) {
            self._win._select_begin.x = self._win._cursor.x;
            self._win._select_begin.y = self._win._cursor.y;
        }
        self.repaint();
    };

    auto connect_shortcut_pair = [&] (ShortcutPair & pair, Method method) {
        QObject::connect(
            &pair.key,
            &QShortcut::activated,
            &self,
            [&self, method] () {
                on_key_pressed(self, method, true);
            }
        );
        QObject::connect(
            &pair.shift_key,
            &QShortcut::activated,
            &self,
            [&self, method] () {
                on_key_pressed(self, method, false);
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
                on_key_pressed(self, method, false);
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

PatternEditorPanel::PatternEditorPanel(MainWindow * parent)
    : QWidget(parent)
    , _win{*parent}
    , _dummy_history{doc::DocumentCopy{}}
    , _history{_dummy_history}
    , _shortcuts{this}
{
    // Upon application startup, pattern editor panel is focused.
    setFocus();

    // Focus widget on click.
    setFocusPolicy(Qt::ClickFocus);

    setMinimumSize(128, 320);

    calc_font_metrics(*this);
    setup_shortcuts(*this);
    create_image(*this);

    // setAttribute(Qt::WA_Hover);  (generates paint events when mouse cursor enters/exits)
    // setContextMenuPolicy(Qt::CustomContextMenu);

    connect(
        parent,
        &MainWindow::gui_refresh,
        this,
        [this] (MaybeSequencerTime maybe_seq_time) {
            update(maybe_seq_time);
        }
    );
}

void PatternEditorPanel::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);

    create_image(*this);
}

// # Column layout
// See doc.h for documentation of how patterns work.

struct ChannelDraw {
    chip_common::ChipIndex chip;
    chip_common::ChannelIndex channel;
    int xleft;
    int xright;
};

namespace subcolumns = edit::pattern::subcolumns;
using edit::pattern::SubColumn;

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

struct ColumnPx {
    chip_common::ChipIndex chip;
    chip_common::ChannelIndex channel;
    int left_px;
    int right_px;
    SubColumnLayout subcolumns;  // all endpoints lie within [left_px, left_px + width]
};

using MaybeColumnPx = std::optional<ColumnPx>;

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

    auto center_sub = [&x_px, width_per_char] (SubColumnPx & sub, int nchar) {
        int dwidth = width_per_char * nchar;
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
    GridRect const inner_rect
) {
    // Use standard app font for header text.
    painter.setFont(QFont{});

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

        painter.setPen(Qt::white);
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
            QString("%1, %2 asdfasdfasdf").arg(chip).arg(channel)
        );

        draw_header_border(channel_rect);
    }
}


namespace {

// yay inconsistency
using PxInt = int;
//using PxNat = uint32_t;

/// Convert a pattern (technically sequence entry) duration to a display height.
PxInt pixels_from_beat(PatternEditorPanel const & widget, BeatFraction beat) {
    PxInt out = doc::round_to_int(
        beat * widget._rows_per_beat * widget._pixels_per_row
    );
    return out;
}

struct SeqEntryPosition {
    SeqEntryIndex seq_entry_index;
    // top and bottom lie on gridlines like GridRect, not pixels like QRect.
    PxInt top;
    PxInt bottom;
};

enum class Direction {
    Forward,
    Reverse,
};

struct SequenceIteratorState {
    PatternEditorPanel const & _widget;
    doc::Document const & _document;

    // Screen pixels (non-negative, but mark as signed to avoid conversion errors)
    static constexpr PxInt _screen_top = 0;
    const PxInt _screen_bottom;

    // Initialized from _scroll_position.
    SeqEntryIndex _curr_seq_entry_index;
    PxInt _curr_pattern_pos;  // Represents top if Forward, bottom if Reverse.

    static PxInt centered_cursor_pos(PxInt screen_height) {
        return screen_height / 2;
    }

public:
    static std::tuple<SequenceIteratorState, PxInt> make(
        PatternEditorPanel const & widget,  // holds reference
        doc::Document const & document,  // holds reference
        PxInt const screen_height
    ) {
        PxInt const cursor_from_pattern_top =
            pixels_from_beat(widget, widget._win._cursor.y.beat);

        PatternAndBeat scroll_position;
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
            scroll_position = widget._win._cursor.y;

            cursor_from_screen_top = centered_cursor_pos(screen_height);
            pattern_top_from_screen_top =
                cursor_from_screen_top - cursor_from_pattern_top;
        }

        SequenceIteratorState out {
            widget,
            document,
            screen_height,
            scroll_position.seq_entry_index,
            pattern_top_from_screen_top,
        };
        return {out, cursor_from_screen_top};
    }
};

template<Direction direction>
class SequenceIterator : private SequenceIteratorState {
public:
    explicit SequenceIterator(SequenceIteratorState state) :
        SequenceIteratorState{state}
    {
        if constexpr (direction == Direction::Reverse) {
            _curr_seq_entry_index--;
        }
    }

private:
    bool valid_seq_entry() const {
        return _curr_seq_entry_index < _document.sequence.size();
    }

    /// Precondition: valid_seq_entry() is true.
    inline PxInt curr_pattern_height() const {
        return pixels_from_beat(
            _widget, _document.sequence[_curr_seq_entry_index].nbeats
        );
    }

    /// Precondition: valid_seq_entry() is true.
    inline PxInt curr_pattern_top() const {
        if constexpr (direction == Direction::Forward) {
            return _curr_pattern_pos;
        } else {
            return _curr_pattern_pos - curr_pattern_height();
        }
    }

    /// Precondition: valid_seq_entry() is true.
    inline PxInt curr_pattern_bottom() const {
        if constexpr (direction == Direction::Reverse) {
            return _curr_pattern_pos;
        } else {
            return _curr_pattern_pos + curr_pattern_height();
        }
    }

private:
    inline SeqEntryPosition peek() const {
        return SeqEntryPosition {
            .seq_entry_index = _curr_seq_entry_index,
            .top = curr_pattern_top(),
            .bottom = curr_pattern_bottom(),
        };
    }

public:
    std::optional<SeqEntryPosition> next() {
        if constexpr (direction == Direction::Forward) {
            if (!valid_seq_entry() || _curr_pattern_pos >= _screen_bottom) {
                return std::nullopt;
            }

            SeqEntryPosition out = peek();
            _curr_pattern_pos += curr_pattern_height();
            _curr_seq_entry_index++;
            return out;

        } else {
            if (!valid_seq_entry() || _curr_pattern_pos <= _screen_top) {
                return std::nullopt;
            }

            SeqEntryPosition out = peek();
            _curr_pattern_pos -= curr_pattern_height();
            _curr_seq_entry_index--;  // May overflow to UINT32_MAX. Not UB.
            return out;
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

/// Draw the background lying behind notes/etc.
static void draw_pattern_background(
    PatternEditorPanel & self,
    doc::Document const &document,
    ColumnLayout const & columns,
    QPainter & painter,
    GridRect const inner_rect
) {
    auto & visual = get_app().options().visual;

    #define COMPUTE_DIVIDER_COLOR(OUT, BG, FG) \
        QColor OUT##_divider = lerp_colors(BG, FG, visual.subcolumn_divider_blend);

    COMPUTE_DIVIDER_COLOR(instrument, visual.instrument_bg, visual.instrument)
    COMPUTE_DIVIDER_COLOR(volume, visual.volume_bg, visual.volume)
    COMPUTE_DIVIDER_COLOR(effect, visual.effect_bg, visual.effect)

    int row_right_px = columns.ruler.right_px;
    for (auto & c : reverse(columns.cols)) {
        if (c.has_value()) {
            row_right_px = c->right_px;
            break;
        }
    }

    auto draw_pattern_bg = [&] (SeqEntryPosition const & pos) {
        doc::SequenceEntry const & seq_entry = document.sequence[pos.seq_entry_index];

        // Draw background of cell.
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
                CASE_NO_FG(sc::Note, visual.note_bg)
                CASE(sc::Instrument, visual.instrument_bg, instrument_divider)
                CASE(sc::Volume, visual.volume_bg, volume_divider)
                CASE(sc::EffectName, visual.effect_bg, effect_divider)
                CASE_NO_FG(sc::EffectValue, visual.effect_bg)

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
        BeatFraction const beats_per_row{1, self._rows_per_beat};
        BeatFraction curr_beats = 0;
        for (;
            curr_beats < seq_entry.nbeats;
            curr_beats += beats_per_row, row += 1)
        {
            // Compute row height.
            int ytop = pos.top + self._pixels_per_row * row;
            // End loop(row)

            // Draw gridline along top of row.
            if (curr_beats.denominator() == 1) {
                painter.setPen(visual.gridline_beat);
            } else {
                painter.setPen(visual.gridline_non_beat);
            }
            draw_top_border(painter, QPoint{0, ytop}, QPoint{row_right_px, ytop});
        }
    };

    auto draw_row_numbers = [&] (SeqEntryPosition const & pos) {
        doc::SequenceEntry const & seq_entry = document.sequence[pos.seq_entry_index];

        // Draw rows.
        // Begin loop(row)
        int row = 0;
        BeatFraction const beats_per_row{1, self._rows_per_beat};
        BeatFraction curr_beats = 0;
        for (;
            curr_beats < seq_entry.nbeats;
            curr_beats += beats_per_row, row += 1)
        {
            int ytop = pos.top + self._pixels_per_row * row;

            // Draw ruler labels (numbers).
            if (curr_beats.denominator() == 1) {
                // Draw current beat.
                QString s = format_hex_2((uint8_t) curr_beats.numerator());

                painter.setFont(visual.pattern_font);
                painter.setPen(visual.note_line_beat);

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

    auto draw_patterns = [&] <Direction direction> (
        auto draw_seq_entry, SequenceIteratorState const & seq
    ) {
        auto it = SequenceIterator<direction>{seq};
        while (auto pos = it.next()) {
            draw_seq_entry(*pos);
        }
    };

    auto [seq, cursor_top] =
        SequenceIteratorState::make(self, document, (PxInt) inner_rect.height());

    // this syntax has got to be a joke, right?
    // C++ needs the turbofish so badly
    draw_patterns.template operator()<Direction::Forward>(draw_pattern_bg, seq);
    draw_patterns.template operator()<Direction::Reverse>(draw_pattern_bg, seq);

    // Draw divider "just past right" of each column.
    // This replaces the "note divider" of the next column.
    // The last column draws a divider in the void.
    painter.setPen(visual.channel_divider);

    // Templated function with multiple T.
    auto draw_divider = [&painter, &inner_rect] (auto const & column) {
        auto xright = column.right_px;

        QPoint right_top{xright, inner_rect.top()};
        QPoint right_bottom{xright, inner_rect.bottom()};

        draw_left_border(painter, right_top, right_bottom);
    };

    draw_divider(columns.ruler);
    for (auto & column : columns.cols) {
        if (column) {
            draw_divider(*column);
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

        auto cursor_x = self._win._cursor.x;
        if (cursor_x.column >= columns.cols.size()) {
            cursor_x.column = 0;
            cursor_x.subcolumn = 0;
        }

        // Draw cursor row and cell background.
        if (auto & col = columns.cols[cursor_x.column]) {
            // If cursor is on-screen, draw left/cursor/right.
            auto subcol = col->subcolumns[cursor_x.subcolumn];

            // Draw left background.
            auto left_rect = cursor_row_rect;
            left_rect.set_right(subcol.left_px);
            painter.fillRect(left_rect, bg_grad);

            // Draw right background.
            auto right_rect = cursor_row_rect;
            right_rect.set_left(subcol.right_px);
            painter.fillRect(right_rect, bg_grad);

            // Draw cursor cell background.
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
            // Otherwise just draw background.
            painter.fillRect(cursor_row_rect, bg_grad);
        }
    }

    draw_patterns.template operator()<Direction::Forward>(draw_row_numbers, seq);
    draw_patterns.template operator()<Direction::Reverse>(draw_row_numbers, seq);
}

/// Draw `RowEvent`s positioned at TimeInPattern. Not all events occur at beat boundaries.
static void draw_pattern_foreground(
    PatternEditorPanel & self,
    doc::Document const &document,
    ColumnLayout const & columns,
    QPainter & painter,
    GridRect const inner_rect
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
    DrawText draw_text{painter.font()};

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

    auto draw_seq_entry = [&](doc::SequenceEntry const & seq_entry) {
        for (MaybeColumnPx const & maybe_column : columns.cols) {
            if (!maybe_column) {
                continue;
            }
            ColumnPx const & column = *maybe_column;
            auto xleft = column.left_px;
            auto xright = column.right_px;

            // https://bugs.llvm.org/show_bug.cgi?id=33236
            // the original C++17 spec broke const struct unpacking.
            for (
                doc::TimedRowEvent timed_event
                : seq_entry.chip_channel_events[column.chip][column.channel]
            ) {
                doc::TimeInPattern time = timed_event.time;
                doc::RowEvent row_event = timed_event.v;

                // Compute where to draw row.
                Frac beat = time.anchor_beat;
                Frac row = beat * self._rows_per_beat;
                int yPx = doc::round_to_int(self._pixels_per_row * row);

                // Move painter relative to current row (not cell).
                PainterScope scope{painter};
                painter.translate(0, yPx);

                // Draw top line.
                // TODO add coarse/fine highlight fractions
                QPoint left_top{xleft, 0};
                QPoint right_top{xright, 0};

                QColor note_color;

                if (beat.denominator() == 1) {
                    // Highlighted notes
                    note_color = visual.note_line_beat;
                } else if (row.denominator() == 1) {
                    // Non-highlighted notes
                    note_color = visual.note_line_non_beat;
                } else {
                    // Off-grid misaligned notes (not possible in traditional trackers)
                    note_color = visual.note_line_fractional;
                }

                // Draw text.
                for (auto const & subcolumn : column.subcolumns) {
                    namespace sc = subcolumns;

                    auto draw = [&](QString & text) {
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
                        draw_text.draw_text(
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
                                QString s = gui_fmt::midi_to_note_name(
                                    note_cfg, document.accidental_mode, note
                                );
                                draw(s);
                            }
                        }
                    }
                    CASE(sc::Instrument) {
                        if (row_event.instr) {
                            painter.setPen(visual.instrument);
                            auto s = format_hex_2(uint8_t(*row_event.instr));
                            draw(s);
                        }
                    }

                    #undef CASE
                }

                // Draw top border. Do it after each note clears the background.
                // Exclude the leftmost column, so we don't overwrite channel dividers.
                painter.setPen(note_color);
                int pen_width = painter.pen().width();
                draw_top_border(painter, left_top + QPoint{pen_width, 0}, right_top);
            }
        }
    };

    auto draw_patterns = [&] <Direction direction> (SequenceIteratorState const & seq) {
        auto it = SequenceIterator<direction>{seq};
        while (auto pos = it.next()) {
            PainterScope scope{painter};
            painter.translate(0, pos->top);
            draw_seq_entry(document.sequence[pos->seq_entry_index]);
        }
    };

    auto [seq, cursor_top] =
        SequenceIteratorState::make(self, document, (PxInt) inner_rect.height());

    draw_patterns.template operator()<Direction::Forward>(seq);
    draw_patterns.template operator()<Direction::Reverse>(seq);

    // TODO draw selection
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
        auto cursor_x = self._win._cursor.x;
        auto ncol = columns.cols.size();

        // Handle special-case of past-the-end cursors separately.
        if (cursor_x.column >= ncol) {
            cursor_x.column = 0;
            cursor_x.subcolumn = 0;
        }

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

            draw_header(
                self,
                document,
                columns,
                painter,
                GridRect{QPoint{0, 0}, outer_rect.size()}
            );
        }

        {
            PainterScope scope{painter};

            // Pattern body, relative to entire widget.
            GridRect absolute_rect = canvas_rect;
            absolute_rect.set_top(header::HEIGHT);
            painter.setClipRect(absolute_rect);

            // translate(offset) = the given offset is added to points.
            painter.translate(absolute_rect.left_top());

            // Pattern body, relative to top-left corner.
            GridRect inner_rect{QPoint{0, 0}, absolute_rect.size()};

            // First draw the row background. It lies in a regular grid.

            // TODO Is it possible to only redraw `rect`?
            // By setting the clip region, or skipping certain channels?

            // TODO When does Qt redraw a small rect? On non-compositing desktops?
            // On non-compositing KDE, Qt doesn't redraw when dragging a window on top.
            draw_pattern_background(self, document, columns, painter, inner_rect);

            // Then for each channel, draw all notes in that channel lying within view.
            // Notes may be positioned at fractional beats that do not lie in the grid.
            draw_pattern_foreground(self, document, columns, painter, inner_rect);
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

// # Following audio thread

void PatternEditorPanel::update(timing::MaybeSequencerTime maybe_seq_time) {
    if (maybe_seq_time.has_value()) {
        // Update cursor to sequencer position (from audio thread).
        auto const seq_time = maybe_seq_time.value();

        PatternAndBeat new_cursor_y{seq_time.seq_entry_index, seq_time.beats};

        // Find row.
        for (int curr_row = _rows_per_beat - 1; curr_row >= 0; curr_row--) {

            auto curr_ticks = curr_row / BeatFraction{_rows_per_beat}
                * seq_time.curr_ticks_per_beat;

            if (doc::round_to_int(curr_ticks) <= seq_time.ticks) {
                new_cursor_y.beat += BeatFraction{curr_row, _rows_per_beat};
                break;
            }
        }

        _win._cursor.y = new_cursor_y;
    }

    repaint();
}

// # Cursor movement

static doc::FractionInt frac_prev(BeatFraction frac) {
    return frac_ceil(frac) - 1;
}

static doc::FractionInt frac_next(BeatFraction frac) {
    return frac_floor(frac) + 1;
}

using BeatsToUnits = BeatFraction (*)(PatternEditorPanel const &, BeatFraction);
using UnitsToBeats = BeatFraction (*)(PatternEditorPanel const &, doc::FractionInt);

// Move the cursor, snapping to the nearest unit.

template<BeatsToUnits to_units, UnitsToBeats to_beats>
void move_up(PatternEditorPanel & self) {
    doc::Document const & document = self.get_document();
    auto const & move_cfg = get_app().options().move_cfg;

    auto & cursor_y = self._win._cursor.y;

    auto const orig_unit = to_units(self, cursor_y.beat);
    doc::FractionInt const up_unit = frac_prev(orig_unit);
    doc::FractionInt out_unit;

    if (up_unit >= 0) {
        out_unit = up_unit;

    } else if (move_cfg.wrap_cursor) {
        if (move_cfg.wrap_across_frames) {
            decrement_mod(
                cursor_y.seq_entry_index, (SeqEntryIndex)document.sequence.size()
            );
        }

        auto const & seq_entry = document.sequence[cursor_y.seq_entry_index];
        out_unit = frac_prev(to_units(self, seq_entry.nbeats));

    } else {
        out_unit = 0;
    }

    cursor_y.beat = to_beats(self, out_unit);
}

template<BeatsToUnits to_units, UnitsToBeats to_beats>
void move_down(PatternEditorPanel & self) {
    doc::Document const & document = self.get_document();
    auto const & move_cfg = get_app().options().move_cfg;

    auto & cursor_y = self._win._cursor.y;

    auto const & seq_entry = document.sequence[cursor_y.seq_entry_index];
    auto const num_units = to_units(self, seq_entry.nbeats);

    auto const orig_unit = to_units(self, cursor_y.beat);
    doc::FractionInt const down_unit = frac_next(orig_unit);
    doc::FractionInt out_unit;

    if (down_unit < num_units) {
        out_unit = down_unit;

    } else if (move_cfg.wrap_cursor) {
        if (move_cfg.wrap_across_frames) {
            increment_mod(
                cursor_y.seq_entry_index, (SeqEntryIndex)document.sequence.size()
            );
        }

        out_unit = 0;

    } else {
        // don't move the cursor.
        return;
    }

    cursor_y.beat = to_beats(self, out_unit);
}

// Beat conversion functions

static inline BeatFraction rows_from_beats(
    PatternEditorPanel const & self, BeatFraction beats
) {
    return beats * self._rows_per_beat;
}

template<typename T>
static inline BeatFraction beats_from_rows(
    PatternEditorPanel const & self, T rows
) {
    return rows / BeatFraction{self._rows_per_beat};
}

template<typename T>
static inline BeatFraction beats_from_beats(
    [[maybe_unused]] PatternEditorPanel const & self, T beats
) {
    return beats;
}

// Cursor movement

void PatternEditorPanel::up_pressed() {
    move_up<rows_from_beats, beats_from_rows>(*this);
}

void PatternEditorPanel::down_pressed() {
    move_down<rows_from_beats, beats_from_rows>(*this);
}

void PatternEditorPanel::prev_beat_pressed() {
    move_up<beats_from_beats, beats_from_beats>(*this);
}

void PatternEditorPanel::next_beat_pressed() {
    move_down<beats_from_beats, beats_from_beats>(*this);
}

// TODO depends on horizontal cursor position.
void PatternEditorPanel::prev_event_pressed() {}
void PatternEditorPanel::next_event_pressed() {}

/// To avoid an infinite loop,
/// avoid scrolling more than _ patterns in a single Page Down keystroke.
constexpr int MAX_PAGEDOWN_SCROLL = 16;

void PatternEditorPanel::scroll_prev_pressed() {
    doc::Document const & document = get_document();
    auto const & move_cfg = get_app().options().move_cfg;

    auto & cursor_y = _win._cursor.y;

    cursor_y.beat -= move_cfg.page_down_distance;

    for (int i = 0; i < MAX_PAGEDOWN_SCROLL; i++) {
        if (cursor_y.beat < 0) {
            decrement_mod(
                cursor_y.seq_entry_index, (SeqEntryIndex) document.sequence.size()
            );
            cursor_y.beat += document.sequence[cursor_y.seq_entry_index].nbeats;
        } else {
            break;
        }
    }
}

void PatternEditorPanel::scroll_next_pressed() {
    doc::Document const & document = get_document();
    auto const & move_cfg = get_app().options().move_cfg;

    auto & cursor_y = _win._cursor.y;

    cursor_y.beat += move_cfg.page_down_distance;

    for (int i = 0; i < MAX_PAGEDOWN_SCROLL; i++) {
        auto const & seq_entry = document.sequence[cursor_y.seq_entry_index];
        if (cursor_y.beat >= seq_entry.nbeats) {
            cursor_y.beat -= seq_entry.nbeats;
            increment_mod(
                cursor_y.seq_entry_index, (SeqEntryIndex) document.sequence.size()
            );
        } else {
            break;
        }
    }
}

template<void alter_mod(SeqEntryIndex & x, SeqEntryIndex den)>
inline void switch_seq_entry_index(PatternEditorPanel & self) {
    doc::Document const & document = self.get_document();
    auto & cursor_y = self._win._cursor.y;

    alter_mod(cursor_y.seq_entry_index, (SeqEntryIndex) document.sequence.size());

    BeatFraction nbeats = document.sequence[cursor_y.seq_entry_index].nbeats;

    // If cursor is out of bounds, move to last row in pattern.
    if (cursor_y.beat >= nbeats) {
        BeatFraction rows = rows_from_beats(self, nbeats);
        int prev_row = frac_prev(rows);
        cursor_y.beat = beats_from_rows(self, prev_row);
    }
}

void PatternEditorPanel::prev_pattern_pressed() {
    switch_seq_entry_index<decrement_mod>(*this);
}
void PatternEditorPanel::next_pattern_pressed() {
    switch_seq_entry_index<increment_mod>(*this);
}

using cursor::CursorX;
using cursor::ColumnIndex;
using cursor::SubColumnIndex;

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
There are two cursor models I could use: Inclusive cursors (item indexing),
or exclusive cursors (gridline indexing).

With inclusive cursors, selecting an integer number of columns is janky.
With exclusive cursors, you can get zero-width selections.
And there must be a way to move the cursor "past the end"
to create a selection including the rightmost subcolumn.
If you type in that state, they'll get inserted in the leftmost channel's
note column instead.

Cursor affinity is fun.

I'll either switch to inclusive horizontal cursor movement,
or allow the user to pick in the settings.

Vertical cursor movement is a less severe issue,
since "end of pattern" and "beginning of next" are indistinguishable
except for pressing End a second time, or when inserting notes.

The biggest vertical cursor issue arises if you have a single looping pattern.
If you hold shift+down until the cursor reaches the end of the document,
the cursor should extend to the end of the document, not the beginning.
Similarly, if you hold down until the cursor reaches the end of the document,
then press shift+up, the cursor should extend from the end of the document.
*/

void PatternEditorPanel::left_pressed() {
    doc::Document const & document = get_document();
    ColumnList cols = gen_column_list(*this, document);

    // there's got to be a better way to write this code...
    // an elegant abstraction i'm missing
    auto & cursor_x = _win._cursor.x;

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
}

void PatternEditorPanel::right_pressed() {
    doc::Document const & document = get_document();
    ColumnList cols = gen_column_list(*this, document);

    // Is it worth extracting cursor movement logic to a class?
    auto & cursor_x = _win._cursor.x;
    wrap_cursor(cols, cursor_x);
    cursor_x.subcolumn++;

    if (cursor_x.subcolumn >= nsubcol(cols, cursor_x.column)) {
        cursor_x.subcolumn = 0;
        cursor_x.column++;

        if (cursor_x.column >= ncol(cols)) {
            cursor_x.column = 0;
        }
    }
}

// TODO implement comparison between subcolumn variants,
// so you can hide pan on some but not all channels

// TODO disable wrapping if move_cfg.wrap_cursor is false.
// X coordinate (nchan, 0) may/not be legal, idk yet.

void PatternEditorPanel::scroll_left_pressed() {
    doc::Document const & document = get_document();
    ColumnList cols = gen_column_list(*this, document);

    auto & cursor_x = _win._cursor.x;
    if (cursor_x.column > 0) {
        cursor_x.column--;
    } else {
        cursor_x.column = ncol(cols) - 1;
    }

    cursor_x.subcolumn =
        std::min(cursor_x.subcolumn, nsubcol(cols, cursor_x.column) - 1);
}

void PatternEditorPanel::scroll_right_pressed() {
    doc::Document const & document = get_document();
    ColumnList cols = gen_column_list(*this, document);

    auto & cursor_x = _win._cursor.x;
    cursor_x.column++;
    wrap_cursor(cols, cursor_x);
    cursor_x.subcolumn =
        std::min(cursor_x.subcolumn, nsubcol(cols, cursor_x.column) - 1);
}

// Begin document mutation
namespace ed = edit::pattern;

auto calc_cursor_x(PatternEditorPanel const & self) ->
    std::tuple<doc::ChipIndex, doc::ChannelIndex, SubColumn>
{
    doc::Document const & document = self.get_document();
    auto cursor_x = self._win._cursor.x;

    Column column = gen_column_list(self, document)[cursor_x.column];
    SubColumn subcolumn = column.subcolumns[cursor_x.subcolumn];

    return {column.chip, column.channel, subcolumn};
}

void PatternEditorPanel::toggle_edit_pressed() {
    _edit_mode = !_edit_mode;
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

    auto [chip, channel, subcolumn] = calc_cursor_x(*this);
    _win.push_edit(
        ed::delete_cell(document, chip, channel, subcolumn, _win._cursor.y),
        _win._cursor
    );
}

void note_pressed(
    PatternEditorPanel & self,
    doc::ChipIndex chip,
    doc::ChannelIndex channel,
    doc::Note note
) {
    auto old_cursor = self._win._cursor;
    for (int i = 0; i < self._step; i++) {
        self.down_pressed();
    }

    std::optional<doc::InstrumentIndex> instrument{};
    if (self._win._insert_instrument) {
        instrument = {self._win._instrument};
    }

    self._win.push_edit(
        ed::insert_note(
            self.get_document(), chip, channel, old_cursor.y, note, instrument
        ),
        old_cursor
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

/// Handles events based on physical layout rather than shortcuts.
/// Basically note and effect/hex input only.
void PatternEditorPanel::keyPressEvent(QKeyEvent * event) {
    auto keycode = qkeycode::toKeycode(event);
    fmt::print(
        stderr,
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
                    return;
                }
            }
        }

    }
}

void PatternEditorPanel::keyReleaseEvent(QKeyEvent * event) {
    auto dom_code = qkeycode::toKeycode(event);
    fmt::print(
        stderr,
        "KeyRelease {}=\"{}\", modifier {}, repeat? {}\n",
        dom_code,
        qkeycode::KeycodeConverter::DomCodeToCodeString(dom_code),
        event->modifiers(),
        event->isAutoRepeat()
    );

    Super::keyReleaseEvent(event);
}

// namespace
}
