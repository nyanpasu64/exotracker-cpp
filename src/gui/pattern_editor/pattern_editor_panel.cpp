#include "pattern_editor_panel.h"

#include "chip_kinds.h"

#include <verdigris/wobjectimpl.h>

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QPoint>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <vector>


namespace gui {
namespace pattern_editor {

W_OBJECT_IMPL(PatternEditorPanel)

#define _initDisplay void initDisplay(PatternEditorPanel & self)
_initDisplay;

PatternEditorPanel::PatternEditorPanel(QWidget *parent) :
    QWidget(parent),
    _dummy_history{doc::DocumentCopy{}},
    _history{_dummy_history}
{
    setMinimumSize(128, 320);

    /* Font */
    _headerFont = QApplication::font();
    _headerFont.setPointSize(10);
    _stepFont = QFont("mononoki", 10);
    _stepFont.setStyleHint(QFont::TypeWriter);
    _stepFont.setStyleStrategy(QFont::ForceIntegerMetrics);

    // Check font size
    QFontMetrics metrics(_stepFont);
    _stepFontWidth = metrics.horizontalAdvance('0');
    _stepFontAscend = metrics.ascent();
    _stepFontLeading = metrics.descent() / 2;
    _stepFontHeight = _stepFontAscend + _stepFontLeading;

    /* Width & height */
    _widthSpace = _stepFontWidth / 5 * 2;
    _widthSpaceDbl = _widthSpace * 2;
    _stepNumWidth = _stepFontWidth * 2 + _widthSpace;
    _toneNameWidth = _stepFontWidth * 3;
    _instWidth = _stepFontWidth * 2;
    _volWidth = _stepFontWidth * 2;
    _effIDWidth = _stepFontWidth * 2;
    _effValWidth = _stepFontWidth * 2;
    _effWidth = _effIDWidth + _effValWidth + _widthSpaceDbl;

    initDisplay(*this);

    // setAttribute(Qt::WA_Hover);  (generates paint events when mouse cursor enters/exits)
    // setContextMenuPolicy(Qt::CustomContextMenu);
}

_initDisplay
{
    self._pixmap = std::make_unique<QPixmap>(self.geometry().size());
}

void PatternEditorPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    initDisplay(*this);
}

// Begin reverse function ordering

struct Palette {
    QColor bg{0, 0, 0};
    QPen channel_divider{QColor{64, 64, 64}};
    QPen gridline_beat{QColor{64, 192, 255}};
    QPen gridline_non_beat{QColor{16*2, 56*2, 80*2}};
    QPen note_line_non_beat{QColor{0, 255, 0}};
    QPen note_line_beat{QColor{255, 255, 96}};
};

static Palette palette;

// See doc.h for documentation of how patterns work.

struct ChannelDraw {
    chip_common::ChipIndex chip;
    chip_common::ChannelIndex channel;
    int xleft;
    int xright;
};

// where Callback_of_ChannelDraw: Fn(ChannelDraw)
template<typename Callback_of_ChannelDraw>
void foreach_channel_draw(
    PatternEditorPanel const & pattern_editor,
    doc::Document const & document,
    Callback_of_ChannelDraw callback
) {

    int x_accum = 0;

    // local
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
            int xleft = x_accum;
            int dx_width = pattern_editor._dxWidth;
            int xright = xleft + dx_width;

            callback(ChannelDraw{chip_index, channel_index, xleft, xright});
            x_accum = xright;
        }
    }
}

using history::History;

/// Vertical channel dividers are drawn at fixed locations. Horizontal gridlines and events are not.
/// So draw horizontal lines after of channel dividers.
/// This macro prevents horizontal gridlines from covering up channel dividers.
/// It'll be "fun" when I add support for multi-pixel-wide lines ;)
//#define HORIZ_GRIDLINE(right_top) ((right_top) - QPoint{1, 0})

/// This macro is a no-op and allows horizontal gridlines to cover channel dividers.
#define HORIZ_GRIDLINE(right_top) (right_top)

/// Draw the background lying behind notes/etc.
static void drawRowBg(
    PatternEditorPanel & self, doc::Document const &document, QPainter & painter
) {
    // TODO follow audio thread's active pattern...
    // How does the audio thread track its active row?
    doc::SequenceEntry const & pattern = document.sequence[0];

    // In Qt, (0, 0) is top-left, dx is right, and dy is down.
    foreach_channel_draw(self, document, [&](ChannelDraw channel_draw) {
        auto [chip, channel, xleft, xright] = channel_draw;

        // drawLine() paints the interval [x1, x2] and [y1, y2] inclusive.
        // Subtract 1 so xright doesn't enter the next channel, or ybottom enter the next row.
        // Draw the right border at [xright-1, xright), so it's not overwritten by the next channel at [xright, ...).
        xright -= 1;

        // Begin loop(row)
        int row = 0;
        doc::BeatFraction curr_beats = 0;
        for (;
            curr_beats < pattern.nbeats;
            curr_beats += self._row_duration_beats, row += 1)
        {
            // Compute row height.
            int ytop = self._dyHeightPerRow * row;
            int dy_height = self._dyHeightPerRow;
            int ybottom = ytop + dy_height;
            // End loop(row)

            // drawLine() see above.
            ybottom -= 1;

            QPoint left_top{xleft, ytop};
            // QPoint left_bottom{xleft, ybottom};
            QPoint right_top{xright, ytop};
            QPoint right_bottom{xright, ybottom};

            // Draw background of cell.
            painter.fillRect(QRect{left_top, right_bottom}, palette.bg);

            // Draw divider down right side.
            painter.setPen(palette.channel_divider);
            painter.drawLine(right_top, right_bottom);

            // Draw gridline along top of row.
            if (curr_beats.denominator() == 1) {
                painter.setPen(palette.gridline_beat);
            } else {
                painter.setPen(palette.gridline_non_beat);
            }
            painter.drawLine(left_top, HORIZ_GRIDLINE(right_top));
        }
    });
}

/// Draw `RowEvent`s positioned at TimeInPattern. Not all events occur at beat boundaries.
static void drawRowEvents(
    PatternEditorPanel & self, doc::Document const &document, QPainter & painter
) {
    using Frac = doc::BeatFraction;

    doc::SequenceEntry const & pattern = document.sequence[0];

    foreach_channel_draw(self, document, [&](ChannelDraw channel_draw) {
        auto [chip, channel, xleft, xright] = channel_draw;

        // drawLine() paints the interval [x1, x2]. Subtract 1 so [xleft, xright-1] doesn't enter the next channel.
        xright -= 1;

        // https://bugs.llvm.org/show_bug.cgi?id=33236
        // the original C++17 spec broke const struct unpacking.
        for (
            doc::TimedRowEvent timed_event
            : pattern.chip_channel_events[chip][channel]
        ) {
            doc::TimeInPattern time = timed_event.time;
            // TODO draw the event
            doc::RowEvent row_event = timed_event.v;

            // Compute where to draw row.
            Frac beat = time.anchor_beat;
            Frac beats_per_row = self._row_duration_beats;
            Frac row = beat / beats_per_row;
            int yPx = doc::round_to_int(self._dyHeightPerRow * row);
            QPoint left_top{xleft, yPx};
            QPoint right_top{xright, yPx};

            // Draw top line.
            // TODO highlight differently based on whether beat or not
            // TODO add coarse/fine highlight fractions
            painter.setPen(palette.note_line_non_beat);
            painter.drawLine(left_top, HORIZ_GRIDLINE(right_top));

            // TODO Draw text.
        }
    });
}

static void drawPattern(PatternEditorPanel & self, const QRect &rect) {
    doc::Document const & document = *self._history.get().gui_get_document();

    self._pixmap->fill(Qt::black);

    QPainter paintOffScreen(self._pixmap.get());

    paintOffScreen.translate(-self._viewportPos);
    paintOffScreen.setFont(self._stepFont);

    // First draw the row background. It lies in a regular grid.
    // TODO only redraw `rect`??? how 2 partial redraw???
    // i assume Qt will always use full-widget rect???
    drawRowBg(self, document, paintOffScreen);
    drawRowEvents(self, document, paintOffScreen);

    // Then for each channel, draw all notes in that channel lying within view.
    // Notes may be positioned at fractional beats that do not lie in the grid.

    // Draw pixmap onto this widget.
    QPainter paintOnScreen(&self);
    paintOnScreen.drawPixmap(rect, *self._pixmap);
}


void PatternEditorPanel::paintEvent(QPaintEvent *event)
{
    drawPattern(*this, event->rect());
}

// namespaces
}
}
