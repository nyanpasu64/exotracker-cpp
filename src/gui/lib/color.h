#pragma once

#include <QColor>

namespace gui::lib::color {

inline qreal lerp(qreal x, qreal y, qreal position) {
    return x + position * (y - x);
}

/// Blends two colors in linear color space.
/// Produces better results on both light and dark themes,
/// than integer blending (which is too dark).
QColor lerp_colors(QColor c1, QColor c2, qreal position);

QColor lerp_srgb(QColor c1, QColor c2, qreal position);

}
