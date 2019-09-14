#include "pattern_editor_panel.h"
#include "util/macros.h"
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

#define _initDisplay void initDisplay(PatternEditorPanel & self)
_initDisplay;

PatternEditorPanel::PatternEditorPanel(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(320, 200);

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
/// Draw the background lying behind notes/etc.
void drawRowBg(PatternEditorPanel & self, int maxWidth) {
    QPainter painter(self.pixmap_.get());

    painter.translate(-self.viewportPos);
    painter.setFont(self.stepFont_);

    LOOP(row, 16) {
        QPoint ptTopLeft{0, self.dyHeightPerRow * row};

        QColor bg {128, 192, 255};
        painter.fillRect(QRect{ptTopLeft, ptTopLeft + QPoint{self.dxWidth, self.dyHeightPerRow}}, bg);

        QPen colorLineTop;
        colorLineTop.setColor(QColor{255, 255, 255});
        painter.setPen(colorLineTop);
        painter.drawLine(ptTopLeft, ptTopLeft + QPoint{self.dxWidth, 0});
    }
}


void drawPattern(PatternEditorPanel & self, const QRect &rect) {
    // int maxWidth = std::min(geometry().width(), TracksWidthFromLeftToEnd_);
    int maxWidth = self.geometry().width();

    self.pixmap_->fill(Qt::black);

    // First draw the row background. It lies in a regular grid.
    drawRowBg(self, maxWidth);

    // Then for each channel, draw all notes in that channel lying within view.
    // Notes may be positioned at fractional beats that do not lie in the grid.

    // Draw pixmap onto this widget.
    QPainter painter(&self);
    painter.drawPixmap(rect, *self.pixmap_);
}


void PatternEditorPanel::paintEvent(QPaintEvent *event)
{
    drawPattern(*this, event->rect());
}

// namespaces
}
}