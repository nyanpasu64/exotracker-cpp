#define PatternEditorPanel_INTERNAL public
#include "pattern_editor_panel.h"

#include "gui/lib/color.h"
#include "gui/lib/format.h"
#include "gui/lib/painter_ext.h"
#include "chip_kinds.h"
#include "util/compare.h"

#include <verdigris/wobjectimpl.h>

#include <QApplication>
#include <QDebug>  // unused
#include <QGradient>
#include <QFontMetrics>
#include <QPainter>
#include <QPoint>
#include <QRect>

#include <algorithm>  // std::max
#include <optional>
#include <tuple>
#include <type_traits>  // is_same_v
#include <variant>
#include <vector>

namespace gui {
namespace pattern_editor {

using gui::lib::color::lerp;
using gui::lib::color::lerp_colors;
using gui::lib::color::lerp_srgb;
using gui::lib::format::format_hex_1;
using gui::lib::format::format_hex_2;
namespace gui_fmt = gui::lib::format;
using namespace gui::lib::painter_ext;

W_OBJECT_IMPL(PatternEditorPanel)

/*
TODO:
- Recompute font metrics when fonts change (set_font()?) or screen DPI changes.
- QPainter::setPen(QColor) sets the pen width to 1 pixel.
  If we add custom pen width support (wider at large font metrics),
  this overload must be banned.
- On high DPI, font metrics automatically scale,
  but dimensions measured in pixels (like header height) don't.
- Should we remove _image and draw directly to the widget?
- Follow audio thread's location (pattern/row), when audio thread is playing.
*/

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

namespace columns {
    constexpr int EXTRA_WIDTH_DIVISOR = 3;

    // TODO switch to 3-digit ruler/space in decimal mode?
    constexpr int RULER_DIGITS = 2;

    // If I label fractional beats, this needs to increase to 3 or more.
    constexpr int RULER_WIDTH_CHARS = 2;
}

namespace header {
    constexpr int HEIGHT = 40;

    constexpr int TEXT_X = 8;
    constexpr int TEXT_Y = 20;
}

constexpr QColor BLACK{0, 0, 0};
constexpr qreal BG_COLORIZE = 0.05;

static constexpr QColor gray(int value) {
    return QColor{value, value, value};
}

struct FontTweaks {
    int width_adjust = 0;

    // To move text down, increase pixels_above_text and decrease pixels_below_text.
    int pixels_above_text = 1;
    int pixels_below_text = -1;
};

// TODO Palette should use QColor, not QPen.
// Line widths should be configured elsewhere, possibly based on DPI.
struct PatternAppearance {
    QColor overall_bg = gray(48);

    /// Vertical line to the right of each channel.
    QColor channel_divider = gray(160);

    /// Background gridline color.
    QColor gridline_beat = gray(128);
    QColor gridline_non_beat = gray(80);

    /// Cursor color.
    QColor cursor_row{0, 224, 255};

    /// Foreground line color, also used as note text color.
    QColor note_line_beat{255, 255, 96};
    QColor note_line_non_beat{0, 255, 0};
    QColor note_line_fractional{0, 224, 255};
    QColor note_bg = lerp_colors(BLACK, note_line_beat, BG_COLORIZE);

    /// Instrument text color.
    QColor instrument{128, 255, 128};
    QColor instrument_bg = lerp_colors(BLACK, instrument, BG_COLORIZE);

    // Volume text color.
    QColor volume{0, 255, 255};
    QColor volume_bg = lerp_colors(BLACK, volume, BG_COLORIZE);

    // Effect name color.
    QColor effect{255, 128, 128};
    QColor effect_bg = lerp_colors(BLACK, effect, BG_COLORIZE);

    /// How bright to make subcolumn dividers.
    /// At 0, dividers are the same color as the background.
    /// At 1, dividers are the same color as foreground text.
    qreal subcolumn_divider_blend = 0.15;

    /// Fonts to use.
    /// Initialized in PatternEditorPanel() constructor.
    QFont header_font;
    QFont pattern_font;

    FontTweaks font_tweaks;
};

static PatternAppearance cfg;

static PatternFontMetrics calc_single_font_metrics(QFont & font) {
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
        .width=width + cfg.font_tweaks.width_adjust,
        .ascent=metrics.ascent(),
        .descent=metrics.descent()
    };
}

static void calc_font_metrics(PatternEditorPanel & self) {
    self._pattern_font_metrics = calc_single_font_metrics(cfg.pattern_font);

    self._pixels_per_row = std::max(
        cfg.font_tweaks.pixels_above_text
            + self._pattern_font_metrics.ascent
            + self._pattern_font_metrics.descent
            + cfg.font_tweaks.pixels_below_text,
        1
    );
}

PatternEditorPanel::PatternEditorPanel(QWidget *parent) :
    QWidget(parent),
    _dummy_history{doc::DocumentCopy{}},
    _history{_dummy_history}
{
    setMinimumSize(128, 320);

    /* Font */
    cfg.header_font = QApplication::font();

    cfg.pattern_font = QFont("dejavu sans mono", 9);
    cfg.pattern_font.setStyleHint(QFont::TypeWriter);

    calc_font_metrics(*this);
    create_image(*this);

    // setAttribute(Qt::WA_Hover);  (generates paint events when mouse cursor enters/exits)
    // setContextMenuPolicy(Qt::CustomContextMenu);
}

void PatternEditorPanel::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);

    create_image(*this);
}

// See doc.h for documentation of how patterns work.

struct ChannelDraw {
    chip_common::ChipIndex chip;
    chip_common::ChannelIndex channel;
    int xleft;
    int xright;
};

namespace subcolumn_types {
    struct Note {
        COMPARABLE(Note, ())
    };
    struct Instrument {
        COMPARABLE(Instrument, ())
    };
    struct Volume {
        COMPARABLE(Volume, ())
    };
    struct EffectName {
        uint8_t effect_col;
        COMPARABLE(EffectName, (effect_col))
    };
    struct EffectValue {
        uint8_t effect_col;
        COMPARABLE(EffectValue, (effect_col))
    };

    using SubColumnType = std::variant<
        Note, Instrument, Volume, EffectName, EffectValue
    >;
}

using subcolumn_types::SubColumnType;
using SubColumnIndex = uint32_t;

/// One colum that the cursor can move into.
struct SubColumn {
    SubColumnType type;

    // Determines the boundaries for click/selection handling.
    int left_px = 0;
    int right_px = 0;

    // Center for text rendering.
    qreal center_px = 0.0;

    SubColumn(SubColumnType type) : type{type} {}
};

using SubColumns = std::vector<SubColumn>;

struct Column {
    chip_common::ChipIndex chip;
    chip_common::ChannelIndex channel;
    int left_px;
    int right_px;
    SubColumns subcolumns;  // all endpoints lie within [left_px, left_px + width]
};

struct ColumnLayout {
    SubColumn ruler;
    std::vector<Column> cols;
};

/// Compute where on-screen to draw each pattern column.
static ColumnLayout gen_column_layout(
    PatternEditorPanel const & self,
    doc::Document const & document,
    int channel_divider_width
) {
    int const width_per_char = self._pattern_font_metrics.width;
    int const extra_width = width_per_char / columns::EXTRA_WIDTH_DIVISOR;

    int x_px = 0;

    auto add_padding = [&x_px, extra_width] () {
        x_px += extra_width;
    };

    auto begin_sub = [&x_px, add_padding] (SubColumn & sub, bool pad = true) {
        sub.left_px = x_px;
        if (pad) {
            add_padding();
        }
    };

    auto center_sub = [&x_px, width_per_char] (SubColumn & sub, int nchar) {
        int dwidth = width_per_char * nchar;
        sub.center_px = x_px + dwidth / qreal(2.0);
        x_px += dwidth;
    };

    auto end_sub = [&x_px, add_padding] (SubColumn & sub, bool pad = true) {
        if (pad) {
            add_padding();
        }
        sub.right_px = x_px;
    };

    // SubColumnType doesn't matter.
    SubColumn ruler{subcolumn_types::Note{}};

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

            SubColumns subcolumns;
            // TODO change doc to list how many effect colums there are

            auto append_subcolumn = [&subcolumns, begin_sub, center_sub, end_sub] (
                SubColumnType type,
                int nchar,
                bool pad_left = true,
                bool pad_right = true
            ) {
                SubColumn sub{type};

                begin_sub(sub, pad_left);
                center_sub(sub, nchar);
                end_sub(sub, pad_right);

                subcolumns.push_back(sub);
            };

            // Notes are 3 characters wide.
            append_subcolumn(subcolumn_types::Note{}, 3);

            // TODO configurable column hiding (one checkbox per column type?)
            // Instruments are 2 characters wide.
            append_subcolumn(subcolumn_types::Instrument{}, 2);

            // TODO Document::get_volume_width(chip_index, chan_index)
            // Volumes are 2 characters wide.
            append_subcolumn(subcolumn_types::Volume{}, 2);

            for (uint8_t effect_col = 0; effect_col < 1; effect_col++) {
                // Effect names are 2 characters wide and only have left padding.
                append_subcolumn(
                    subcolumn_types::EffectName{effect_col}, 2, true, false
                );
                // Effect values are 2 characters wide and only have right padding.
                append_subcolumn(
                    subcolumn_types::EffectValue{effect_col}, 2, false, true
                );
            }

            // The rightmost subcolumn has one extra pixel for the channel divider.
            x_px += channel_divider_width;
            end_sub(subcolumns[subcolumns.size() - 1], false);

            column_layout.cols.push_back(Column{
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

// TODO fn gen_column_list(doc, ColumnView) generates order of all sub/columns
// (not just visible columns) for keyboard-based movement rather than rendering.
// Either flat or nested, not sure yet.


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
    painter.setFont(cfg.header_font);

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
        // So each channel only draws its right border.
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
    for (Column const & column : columns.cols) {
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
PxInt pixels_from_beat(PatternEditorPanel const & widget, doc::BeatFraction beat) {
    PxInt out = doc::round_to_int(
        beat / widget._beats_per_row * widget._pixels_per_row
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
            pixels_from_beat(widget, widget._cursor_y.curr_beat);

        PatternAndBeat scroll_position;
        PxInt pattern_top_from_screen_top;
        PxInt cursor_from_screen_top;

        if (widget._free_scroll_position.has_value()) {
            // Free scrolling.
            scroll_position = *widget._free_scroll_position;

            PxInt const screen_top_from_pattern_top =
                pixels_from_beat(widget, scroll_position.curr_beat);
            pattern_top_from_screen_top = -screen_top_from_pattern_top;
            cursor_from_screen_top =
                cursor_from_pattern_top + pattern_top_from_screen_top;
        } else {
            // Cursor-locked scrolling.
            scroll_position = widget._cursor_y;

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


/// Vertical channel dividers are drawn at fixed locations. Horizontal gridlines and events are not.
/// So draw horizontal lines after of channel dividers.
/// This macro prevents horizontal gridlines from covering up channel dividers.
#define HORIZ_GRIDLINE(right_top, channel_divider_width) \
    ((right_top) - QPoint{(channel_divider_width), 0})


/// Draw the background lying behind notes/etc.
static void draw_pattern_background(
    PatternEditorPanel & self,
    doc::Document const &document,
    ColumnLayout const & columns,
    QPainter & painter,
    GridRect const inner_rect
) {
    #define COMPUTE_DIVIDER_COLOR(OUT, BG, FG) \
        QColor OUT##_divider = lerp_colors(BG, FG, cfg.subcolumn_divider_blend);

    COMPUTE_DIVIDER_COLOR(instrument, cfg.instrument_bg, cfg.instrument)
    COMPUTE_DIVIDER_COLOR(volume, cfg.volume_bg, cfg.volume)
    COMPUTE_DIVIDER_COLOR(effect, cfg.effect_bg, cfg.effect)

    int row_right_px = columns.ruler.right_px;
    if (columns.cols.size()) {
        row_right_px = columns.cols[columns.cols.size() - 1].right_px;
    }

    auto draw_seq_entry = [&](doc::SequenceEntry const & seq_entry) {
        // Draw rows.
        // Begin loop(row)
        int row = 0;
        doc::BeatFraction curr_beats = 0;
        for (;
            curr_beats < seq_entry.nbeats;
            curr_beats += self._beats_per_row, row += 1)
        {
            // Compute row height.
            int ytop = self._pixels_per_row * row;
            int dy_height = self._pixels_per_row;
            int ybottom = ytop + dy_height;
            // End loop(row)

            // Draw ruler labels (numbers).
            if (curr_beats.denominator() == 1) {
                // Draw current beat.
                QString s = format_hex_2((uint8_t) curr_beats.numerator());

                painter.setFont(cfg.pattern_font);
                painter.setPen(cfg.note_line_beat);

                DrawText draw_text{cfg.pattern_font};
                draw_text.draw_text(
                    painter,
                    columns.ruler.center_px,
                    ytop + cfg.font_tweaks.pixels_above_text,
                    Qt::AlignTop | Qt::AlignHCenter,
                    s
                );
            }
            // Don't label non-beat rows for the time being.

            // Draw background of cell.
            for (Column const & column : columns.cols) {
                for (SubColumn const & sub : column.subcolumns) {
                    GridRect sub_rect{
                        QPoint{sub.left_px, ytop}, QPoint{sub.right_px, ybottom}
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

                    namespace st = subcolumn_types;

                    // Don't draw the note column's divider line,
                    // since it lies right next to the previous channel's channel divider.
                    CASE_NO_FG(st::Note, cfg.note_bg)
                    CASE(st::Instrument, cfg.instrument_bg, instrument_divider)
                    CASE(st::Volume, cfg.volume_bg, volume_divider)
                    CASE(st::EffectName, cfg.effect_bg, effect_divider)
                    CASE_NO_FG(st::EffectValue, cfg.effect_bg)

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

            // Draw gridline along top of row.
            if (curr_beats.denominator() == 1) {
                painter.setPen(cfg.gridline_beat);
            } else {
                painter.setPen(cfg.gridline_non_beat);
            }
            draw_top_border(painter, QPoint{0, ytop}, QPoint{row_right_px, ytop});
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

    auto [seq, cursor_y] =
        SequenceIteratorState::make(self, document, (PxInt) inner_rect.height());

    // this syntax has got to be a joke, right?
    // C++ needs the turbofish so badly
    draw_patterns.template operator()<Direction::Forward>(seq);
    draw_patterns.template operator()<Direction::Reverse>(seq);

    // Draw divider down right side of each column.
    painter.setPen(cfg.channel_divider);

    auto draw_divider = [&painter, &inner_rect] (auto column) {
        auto xright = column.right_px;

        QPoint right_top{xright, inner_rect.top()};
        QPoint right_bottom{xright, inner_rect.bottom()};

        draw_right_border(painter, right_top, right_bottom);
    };

    draw_divider(columns.ruler);
    for (Column const & column : columns.cols) {
        draw_divider(column);
    }
}

constexpr gui_fmt::NoteNameConfig note_cfg {
    .bottom_octave = -1,
    .accidental_mode = gui_fmt::Accidentals::Sharp,
    .sharp_char = '#',
    .flat_char = 'b',
    .natural_char = 0xB7,
};

/// Draw `RowEvent`s positioned at TimeInPattern. Not all events occur at beat boundaries.
static void draw_pattern_foreground(
    PatternEditorPanel & self,
    doc::Document const &document,
    ColumnLayout const & columns,
    QPainter & painter,
    GridRect const inner_rect
) {
    using Frac = doc::BeatFraction;

    // Take a backup of _image to self._temp_image.
    {
        QPainter temp_painter{&self._temp_image};
        temp_painter.drawImage(0, 0, self._image);
    }

    painter.setFont(cfg.pattern_font);
    DrawText draw_text{painter.font()};

    // Dimensions of the note cut/release rectangles.
    int const rect_height = std::max(qRound(self._pixels_per_row / 8.0), 2);
    qreal const rect_width = 2.25 * self._pattern_font_metrics.width;

    // Shift the rectangles vertically a bit, when rounding off sizes.
    constexpr qreal Y_OFFSET = 0.0;

    auto draw_note_cut = [&self, &painter, rect_height, rect_width] (
        SubColumn const & subcolumn, QColor color
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
        SubColumn const & subcolumn, QColor color
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
        for (Column const & column : columns.cols) {
            auto xleft = column.left_px;
            auto xright = column.right_px;

            // https://bugs.llvm.org/show_bug.cgi?id=33236
            // the original C++17 spec broke const struct unpacking.
            for (
                doc::TimedRowEvent timed_event
                : seq_entry.chip_channel_events[column.chip][column.channel]
            ) {
                doc::TimeInPattern time = timed_event.time;
                // TODO draw the event
                doc::RowEvent row_event = timed_event.v;

                // Compute where to draw row.
                Frac beat = time.anchor_beat;
                Frac row = beat / self._beats_per_row;
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
                    note_color = cfg.note_line_beat;
                } else if (row.denominator() == 1) {
                    // Non-highlighted notes
                    note_color = cfg.note_line_non_beat;
                } else {
                    // Off-grid misaligned notes (not possible in traditional trackers)
                    note_color = cfg.note_line_fractional;
                }

                // Draw text.
                for (auto const & subcolumn : column.subcolumns) {
                    namespace st = subcolumn_types;

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
                            cfg.font_tweaks.pixels_above_text,
                            Qt::AlignTop | Qt::AlignHCenter,
                            text
                        );
                    };

                    #define CASE(VARIANT) \
                        if (std::holds_alternative<VARIANT>(subcolumn.type))

                    CASE(st::Note) {
                        if (row_event.note) {
                            auto note = *row_event.note;

                            if (note.is_cut()) {
                                draw_note_cut(subcolumn, note_color);
                            } else if (note.is_release()) {
                                draw_release(subcolumn, note_color);
                            } else {
                                painter.setPen(note_color);
                                QString s = gui_fmt::midi_to_note_name(note_cfg, note);
                                draw(s);
                            }
                        }
                    }
                    CASE(st::Instrument) {
                        if (row_event.instr) {
                            painter.setPen(cfg.instrument);
                            auto s = format_hex_2(uint8_t(*row_event.instr));
                            draw(s);
                        }
                    }

                    #undef CASE
                }

                // Draw top border. Do it after each note clears the background.
                painter.setPen(note_color);
                draw_top_border(
                    painter, left_top, HORIZ_GRIDLINE(right_top, painter.pen().width())
                );
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

    auto [seq, cursor_y] =
        SequenceIteratorState::make(self, document, (PxInt) inner_rect.height());

    draw_patterns.template operator()<Direction::Forward>(seq);
    draw_patterns.template operator()<Direction::Reverse>(seq);

    // Draw cursor.
    // The cursor is drawn on top of channel dividers and note lines/text.
    if (columns.cols.size()) {
        int row_left_px = columns.cols[0].left_px;
        int row_right_px = columns.cols[columns.cols.size() - 1].right_px;

        painter.setPen(cfg.cursor_row);
        draw_top_border(
            painter, QPoint{row_left_px, cursor_y}, QPoint{row_right_px, cursor_y}
        );
    }
}


static void draw_pattern(PatternEditorPanel & self, const QRect repaint_rect) {
    doc::Document const & document = *self._history.get().gui_get_document();

    // TODO maybe only draw repaint_rect? And use Qt::IntersectClip?

    self._image.fill(cfg.overall_bg);

    {
        auto painter = QPainter(&self._image);

        GridRect canvas_rect = self._image.rect();

        ColumnLayout columns = gen_column_layout(self, document, painter.pen().width());

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

// namespaces
}
}
