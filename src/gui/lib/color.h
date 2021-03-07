#pragma once

#include "gui/lib/qt6.h"

#include <QColor>

namespace gui::lib::color {

#if QT6
using ColorF = float;
#else
using ColorF = qreal;
#endif

inline ColorF lerp(ColorF x, ColorF y, ColorF position) {
    return x + position * (y - x);
}

/// Blends two colors in linear color space.
/// Produces better results on both light and dark themes,
/// than integer blending (which is too dark).
QColor lerp_colors(QColor c1, QColor c2, ColorF position);

/// Blends two colors numerically by their RGB values in gamma space.
/// Not recommended.
QColor lerp_srgb(QColor c1, QColor c2, ColorF position);

}
