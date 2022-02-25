#pragma once

#include <QPaintDevice>
#include <QImage>
#include <QWidget>

namespace gui::lib::dpi {

inline qreal dpi_fraction(QPaintDevice * pd) {
    return pd->logicalDpiY() / qreal(96.0);
}

/// TODO find a reliable "window DPI changed" signal
inline qreal dpi_scale(QPaintDevice * pd, qreal distance) {
    return dpi_fraction(pd) * distance;
}

inline QImage scaledQImage(QSize size, QImage::Format format, int ratio)
{
    QImage out(size * ratio, format);
    out.setDevicePixelRatio(ratio);
    return out;
}

inline QImage scaledQImage(int width, int height, QImage::Format format, int ratio)
{
    QImage out(width * ratio, height * ratio, format);
    out.setDevicePixelRatio(ratio);
    return out;
}

inline int iRatio(QWidget const& w)
{
    // devicePixelRatio is int on Qt 5 and qreal on Qt 6.
    // This shouldn't result in *too many* behavior differences though,
    // since devicePixelRatioF is an integer on Qt 5,
    // unless KDE sets QT_SCREEN_SCALE_FACTORS (we can't workaround)
    // or we set DPI scaling to PassThrough (we don't).
    auto ratio = w.devicePixelRatio();

    // Fails on Linux KDE due to https://bugreports.qt.io/browse/QTBUG-95930.
    // Q_ASSERT((int) ratio == ratio);

    return (int) ratio;
}

}
