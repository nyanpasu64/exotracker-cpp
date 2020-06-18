#include "painter_ext.h"

namespace gui::lib::painter_ext {

void DrawText::draw_text(QPainter & painter, const qreal x, const qreal y, Qt::Alignment align, const QString & text, QRectF * boundingRect) {
    qreal const down = 32767.0;
    qreal const right = down;

    qreal left = x;
    qreal top = y;

    if (align & Qt::AlignHCenter) {
        left -= right/2.0;
    }
    else if (align & Qt::AlignRight) {
        left -= right;
    }

    // Qt::AlignTop properly adds space above lowercase characters.
    // But for tall Unicode characters, the baseline will end up too low.
    if (align & Qt::AlignTop) {
        // do nothing
    }
    else if (align & Qt::AlignVCenter) {
        top -= down/2.0;
    }
    else if (align & Qt::AlignBottom) {
        top -= down;
    }
    else {
        // Emulate baseline alignment (AKA calling drawText() with a point).

        // https://code.woboq.org/qt5/qtbase/src/gui/painting/qpainter.cpp.html
        // Qt drawText(rect) has a simple "no-shaping" mode (undocumented Qt::TextBypassShaping, will be removed in Qt 6)
        // and a complex "glyph-script-shaping" mode.
        // My code will only be using drawText() for ASCII characters.

        // Each codepath computes font descent differently.
        // The simple mode probably constructs one QFontEngine per call, to compute descent.
        // The complex mode does weird things.

        int down_descent = _descent;

        align |= Qt::AlignBottom;
        top -= down;
        top += down_descent;
    }

    QRectF rect{left, top, right, down};
    painter.drawText(rect, int(align), text, boundingRect);
}

}
