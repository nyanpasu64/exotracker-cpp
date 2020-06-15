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
- Extract font calculation to calc_font_metrics(),
  to be called whenever fonts change (set_font()?).
- Also recompute font metrics when screen DPI changes.
- QPainter::setPen(QColor) sets the pen width to 1 pixel.
  If we add custom pen width support (wider at large font metrics),
  this overload must be banned.
- On high DPI, font metrics automatically scale,
  but dimensions measured in pixels (like header height) don't.
- Should we remove _image and draw directly to the widget?
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
}

// TODO make it a class for user configurability
namespace font_tweaks {
    constexpr int WIDTH_ADJUST = 0;

    // To move text down, increase PIXELS_ABOVE_TEXT and decrease PIXELS_BELOW_TEXT.
    constexpr int PIXELS_ABOVE_TEXT = 1;
    constexpr int PIXELS_BELOW_TEXT = -1;
}

namespace header {
    constexpr int HEIGHT = 40;

    constexpr int TEXT_X = 8;
    constexpr int TEXT_Y = 20;
}

PatternEditorPanel::PatternEditorPanel(QWidget *parent) :
    QWidget(parent),
    _dummy_history{doc::DocumentCopy{}},
    _history{_dummy_history}
{
    setMinimumSize(128, 320);

    /* Font */
    _header_font = QApplication::font();

    _pattern_font = QFont("dejavu sans mono", 9);
    _pattern_font.setStyleHint(QFont::TypeWriter);

    // Process pattern font metrics
    {
        QFontMetrics metrics{_pattern_font};
        qDebug() << metrics.ascent();
        qDebug() << metrics.descent();
        qDebug() << metrics.height();

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

        _pattern_font_metrics = PatternFontMetrics{
            .width=width + font_tweaks::WIDTH_ADJUST,
            .ascent=metrics.ascent(),
            .descent=metrics.descent()
        };

        _dy_height_per_row =
            font_tweaks::PIXELS_ABOVE_TEXT
            + _pattern_font_metrics.ascent
            + _pattern_font_metrics.descent
            + font_tweaks::PIXELS_BELOW_TEXT;
    }

    create_image(*this);

    // setAttribute(Qt::WA_Hover);  (generates paint events when mouse cursor enters/exits)
    // setContextMenuPolicy(Qt::CustomContextMenu);
}

void PatternEditorPanel::resizeEvent(QResizeEvent *event)
{
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

namespace layout {
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
}

using ColumnLayout = std::vector<layout::Column>;

/// Compute where on-screen to draw each pattern column.
static ColumnLayout gen_column_layout(
    PatternEditorPanel const & self,
    doc::Document const & document,
    int channel_divider_width
) {
    int const width_per_char = self._pattern_font_metrics.width;
    int const extra_width = width_per_char / columns::EXTRA_WIDTH_DIVISOR;

    ColumnLayout column_layout;
    int x_px = 0;

    auto add_padding = [&x_px, extra_width] () {
        x_px += extra_width;
    };

    auto begin_sub = [&x_px, add_padding] (layout::SubColumn & sub, bool pad = true) {
        sub.left_px = x_px;
        if (pad) {
            add_padding();
        }
    };

    auto center_sub = [&x_px, width_per_char] (layout::SubColumn & sub, int nchar) {
        int dwidth = width_per_char * nchar;
        sub.center_px = x_px + dwidth / qreal(2.0);
        x_px += dwidth;
    };

    auto end_sub = [&x_px, add_padding] (layout::SubColumn & sub, bool pad = true) {
        if (pad) {
            add_padding();
        }
        sub.right_px = x_px;
    };

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

            layout::SubColumns subcolumns;
            // TODO change doc to list how many effect colums there are

            auto append_subcolumn = [&subcolumns, begin_sub, center_sub, end_sub] (
                SubColumnType type,
                int nchar,
                bool pad_left = true,
                bool pad_right = true
            ) {
                layout::SubColumn sub{type};

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

            column_layout.push_back(layout::Column{
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
// columns, palette, and document are identical between different drawing phases.
// inner_rect is not.

static void draw_header(
    PatternEditorPanel & self,
    doc::Document const &document,
    ColumnLayout const & columns,
    QPainter & painter,
    GridRect const inner_rect
) {
    painter.setFont(self._header_font);

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

    // Draw each channel's outline and text.
    for (layout::Column const & column : columns) {
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
    }
}


constexpr QColor BLACK{0, 0, 0};
constexpr qreal BG_COLORIZE = 0.05;

static constexpr QColor gray(int value) {
    return QColor{value, value, value};
}

// TODO Palette should use QColor, not QPen.
// Line widths should be configured elsewhere, possibly based on DPI.
struct PatternPalette {
    QColor overall_bg = gray(48);

    /// Vertical line to the right of each channel.
    QColor channel_divider = gray(160);

    /// Background gridline color.
    QColor gridline_beat = gray(128);
    QColor gridline_non_beat = gray(80);

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
};

static PatternPalette palette;


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
    // TODO follow audio thread's active pattern...
    // How does the audio thread track its active row?
    doc::SequenceEntry const & pattern = document.sequence[0];

    #define COMPUTE_DIVIDER_COLOR(OUT, BG, FG) \
        QColor OUT##_divider = lerp_colors(BG, FG, palette.subcolumn_divider_blend);

    COMPUTE_DIVIDER_COLOR(instrument, palette.instrument_bg, palette.instrument)
    COMPUTE_DIVIDER_COLOR(volume, palette.volume_bg, palette.volume)
    COMPUTE_DIVIDER_COLOR(effect, palette.effect_bg, palette.effect)

    for (layout::Column const & column : columns) {
        auto xleft = column.left_px;
        auto xright = column.right_px;

        // Draw rows.
        // Begin loop(row)
        int row = 0;
        doc::BeatFraction curr_beats = 0;
        for (;
            curr_beats < pattern.nbeats;
            curr_beats += self._row_duration_beats, row += 1)
        {
            // Compute row height.
            int ytop = self._dy_height_per_row * row;
            int dy_height = self._dy_height_per_row;
            int ybottom = ytop + dy_height;
            // End loop(row)

            QPoint left_top{xleft, ytop};
            // QPoint left_bottom{xleft, ybottom};
            QPoint right_top{xright, ytop};
            QPoint right_bottom{xright, ybottom};

            // Draw background of cell.
            for (layout::SubColumn const & sub : column.subcolumns) {
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
                CASE_NO_FG(st::Note, palette.note_bg)
                CASE(st::Instrument, palette.instrument_bg, instrument_divider)
                CASE(st::Volume, palette.volume_bg, volume_divider)
                CASE(st::EffectName, palette.effect_bg, effect_divider)
                CASE_NO_FG(st::EffectValue, palette.effect_bg)

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

            // Draw divider down right side.
            // TODO draw globally, not per cell.
            painter.setPen(palette.channel_divider);
            draw_right_border(painter, right_top, right_bottom);

            // Draw gridline along top of row.
            if (curr_beats.denominator() == 1) {
                painter.setPen(palette.gridline_beat);
            } else {
                painter.setPen(palette.gridline_non_beat);
            }
            draw_top_border(
                painter, left_top, HORIZ_GRIDLINE(right_top, painter.pen().width())
            );
        }
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

    painter.setFont(self._pattern_font);
    DrawText draw_text{painter.font()};

    // Dimensions of the note cut/release rectangles.
    int const rect_height = std::max(qRound(self._dy_height_per_row / 8.0), 2);
    qreal const rect_width = 2.25 * self._pattern_font_metrics.width;

    // Shift the rectangles vertically a bit, when rounding off sizes.
    constexpr qreal Y_OFFSET = 0.0;

    auto draw_note_cut = [&self, &painter, rect_height, rect_width] (
        layout::SubColumn const & subcolumn, QColor color
    ) {
        qreal x1f = subcolumn.center_px - rect_width / 2;
        qreal x2f = x1f + rect_width;
        x1f = round(x1f);
        x2f = round(x2f);

        // Round to integer, so note release has integer gap between lines.
        painter.setPen(QPen{color, qreal(rect_height)});

        qreal y = self._dy_height_per_row * qreal(0.5) + Y_OFFSET;
        painter.drawLine(QPointF{x1f, y}, QPointF{x2f, y});
    };

    auto draw_release = [&self, &painter, rect_height, rect_width] (
        layout::SubColumn const & subcolumn, QColor color
    ) {
        qreal x1f = subcolumn.center_px - rect_width / 2;
        qreal x2f = x1f + rect_width;
        int x1 = qRound(x1f);
        int x2 = qRound(x2f);

        // Round to integer, so note release has integer gap between lines.
        painter.setPen(QPen{color, qreal(rect_height)});

        int ytop = qRound(0.5 * self._dy_height_per_row - 0.5 * rect_height + Y_OFFSET);
        int ybot = ytop + rect_height;

        draw_bottom_border(painter, GridRect::from_corners(x1, ytop, x2, ytop));
        draw_top_border(painter, GridRect::from_corners(x1, ybot, x2, ybot));
    };

    doc::SequenceEntry const & pattern = document.sequence[0];

    for (layout::Column const & column : columns) {
        auto xleft = column.left_px;
        auto xright = column.right_px;

        // https://bugs.llvm.org/show_bug.cgi?id=33236
        // the original C++17 spec broke const struct unpacking.
        for (
            doc::TimedRowEvent timed_event
            : pattern.chip_channel_events[column.chip][column.channel]
        ) {
            doc::TimeInPattern time = timed_event.time;
            // TODO draw the event
            doc::RowEvent row_event = timed_event.v;

            // Compute where to draw row.
            Frac beat = time.anchor_beat;
            Frac beats_per_row = self._row_duration_beats;
            Frac row = beat / beats_per_row;
            int yPx = doc::round_to_int(self._dy_height_per_row * row);

            // Move painter relative to current row (not cell).
            PainterScope scope{painter};
            painter.translate(0, yPx);

            // Draw top line.
            // TODO add coarse/fine highlight fractions
            QPoint left_top{xleft, 0};
            QPoint right_top{xright, 0};

            QColor note_color;
            if (beat.denominator() == 1) {
                note_color = palette.note_line_beat;
            } else if (row.denominator() == 1) {
                note_color = palette.note_line_non_beat;
            } else {
                note_color = palette.note_line_fractional;
            }

            // Draw text.
            for (auto const & subcolumn : column.subcolumns) {
                namespace st = subcolumn_types;

                auto draw = [&](QString & text) {
                    // Clear background using unmodified copy free of rendered text.
                    // Unlike alpha transparency, this doesn't break ClearType
                    // and may be faster as well.
                    // Multiply by 1.5 or 2-ish if character tails are not being cleared.
                    auto clear_height = self._dy_height_per_row;

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
                        font_tweaks::PIXELS_ABOVE_TEXT,
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
                        painter.setPen(palette.instrument);
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
}


static void draw_pattern(PatternEditorPanel & self, const QRect &repaint_rect) {
    doc::Document const & document = *self._history.get().gui_get_document();

    self._image.fill(palette.overall_bg);

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

            GridRect outer_rect = canvas_rect;
            outer_rect.set_top(header::HEIGHT);
            painter.setClipRect(outer_rect);

            GridRect inner_rect{QPoint{0, 0}, outer_rect.size()};

            // translate(offset) = the given offset is added to points.
            painter.translate(QPoint{0, header::HEIGHT});

            painter.translate(-self._viewport_pos);

            // First draw the row background. It lies in a regular grid.

            // TODO Is it possible to only redraw `rect`?
            // By setting the clip region, or skipping certain channels?

            // TODO When does Qt redraw a small rect? On non-compositing desktops?
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


void PatternEditorPanel::paintEvent(QPaintEvent *event)
{
    draw_pattern(*this, event->rect());
}

// namespaces
}
}
