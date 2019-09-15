#include "pattern_editor_panel.h"
#include "util/macros.h"

#include <verdigris/wobjectimpl.h>

#include <QPainter>
#include <QFontMetrics>
#include <QPoint>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QAction>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QMetaMethod>
#include <algorithm>
#include <vector>
#include <utility>
#include <stdexcept>
#include <cstdlib>


namespace gui {
namespace pattern_editor {

W_OBJECT_IMPL(PatternEditorPanel)

#define _initDisplay void initDisplay(PatternEditorPanel & self)
_initDisplay;

PatternEditorPanel::PatternEditorPanel(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(320, 200);
//    TimeInPattern, RowEvent
    channel_data[doc::TimeInPattern{0, 0}] = doc::RowEvent{0};
    channel_data[doc::TimeInPattern{{1, 4}, 0}] = doc::RowEvent{1};
    channel_data[doc::TimeInPattern{{2, 4}, 0}] = doc::RowEvent{2};
    channel_data[doc::TimeInPattern{1, 0}] = doc::RowEvent{4};
    Q_ASSERT(channel_data.size() == 4);

    /* Font */
    headerFont_ = QApplication::font();
    headerFont_.setPointSize(10);
    stepFont_ = QFont("mononoki", 10);
    stepFont_.setStyleHint(QFont::TypeWriter);
    stepFont_.setStyleStrategy(QFont::ForceIntegerMetrics);

    // Check font size
    QFontMetrics metrics(stepFont_);
    stepFontWidth_ = metrics.horizontalAdvance('0');
    stepFontAscend_ = metrics.ascent();
    stepFontLeading_ = metrics.descent() / 2;
    stepFontHeight_ = stepFontAscend_ + stepFontLeading_;

    /* Width & height */
    widthSpace_ = stepFontWidth_ / 5 * 2;
    widthSpaceDbl_ = widthSpace_ * 2;
    stepNumWidth_ = stepFontWidth_ * 2 + widthSpace_;
    toneNameWidth_ = stepFontWidth_ * 3;
    instWidth_ = stepFontWidth_ * 2;
    volWidth_ = stepFontWidth_ * 2;
    effIDWidth_ = stepFontWidth_ * 2;
    effValWidth_ = stepFontWidth_ * 2;
    effWidth_ = effIDWidth_ + effValWidth_ + widthSpaceDbl_;

    initDisplay(*this);

    // setAttribute(Qt::WA_Hover);  (generates paint events when mouse cursor enters/exits)
    // setContextMenuPolicy(Qt::CustomContextMenu);
}

_initDisplay
{
    self.pixmap_ = std::make_unique<QPixmap>(self.geometry().size());
}

void PatternEditorPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    initDisplay(*this);
}

// Begin reverse function ordering

// See document.h for documentation of how patterns work.

/// Draw the background lying behind notes/etc.
void drawRowBg(PatternEditorPanel & self, QPainter & painter) {
    doc::BeatFraction curr_beats = 0;
    int row = 0;
    for (;
         curr_beats < self.nbeats;
         curr_beats += self.row_duration_beats, row += 1)
    {
        QPoint ptTopLeft{0, self.dyHeightPerRow * row};

        QColor bg {128, 192, 255};
        painter.fillRect(QRect{ptTopLeft, ptTopLeft + QPoint{self.dxWidth, self.dyHeightPerRow}}, bg);

        QPen colorLineTop;
        if (curr_beats.denominator() == 1) {
            colorLineTop.setColor(QColor{255, 255, 255});
        } else {
            colorLineTop.setColor(QColor{192, 224, 255});
        }
        painter.setPen(colorLineTop);
        painter.drawLine(ptTopLeft, ptTopLeft + QPoint{self.dxWidth, 0});
    }
}

template <typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

template <typename rational>
inline int round_to_int(rational v)
{
    v = v + typename rational::int_type(sgn(v.numerator())) / 2;
    return boost::rational_cast<int>(v);
}

/// Draw `RowEvent`s positioned at TimeInPattern. Not all events occur at beat boundaries.
void drawRowEvents(PatternEditorPanel & self, QPainter & painter) {
    for (const auto& [time, row_event]: self.channel_data) {
        // Compute where to draw row.
        doc::BeatFraction beat = time.anchor_beat;
        doc::BeatFraction & beats_per_row = self.row_duration_beats;
        doc::BeatFraction row = beat / beats_per_row;
        int yPx = round_to_int(self.dyHeightPerRow * row);
        QPoint ptTopLeft{0, yPx};

        // Draw top line.
        QPen colorLineTop;
        colorLineTop.setColor({255, 0, 0});
        painter.setPen(colorLineTop);
        painter.drawLine(ptTopLeft, ptTopLeft + QPoint{self.dxWidth, 0});

        // TODO Draw text.
    }
}

void drawPattern(PatternEditorPanel & self, const QRect &rect) {
    // int maxWidth = std::min(geometry().width(), TracksWidthFromLeftToEnd_);

    self.pixmap_->fill(Qt::black);

    QPainter paintOffScreen(self.pixmap_.get());

    paintOffScreen.translate(-self.viewportPos);
    paintOffScreen.setFont(self.stepFont_);

    // First draw the row background. It lies in a regular grid.
    // TODO only redraw `rect`??? how 2 partial redraw???
    // i assume Qt will always use full-widget rect???
    drawRowBg(self, paintOffScreen);
    drawRowEvents(self, paintOffScreen);

    // Then for each channel, draw all notes in that channel lying within view.
    // Notes may be positioned at fractional beats that do not lie in the grid.

    // Draw pixmap onto this widget.
    QPainter paintOnScreen(&self);
    paintOnScreen.drawPixmap(rect, *self.pixmap_);
}


void PatternEditorPanel::paintEvent(QPaintEvent *event)
{
    drawPattern(*this, event->rect());
}

// namespaces
}
}
