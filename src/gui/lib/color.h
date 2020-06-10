#pragma once

#include <QColor>
#include <cmath>  // pow

namespace gui::lib::color {

qreal lerp(qreal x, qreal y, qreal position) {
    return x + position * (y - x);
}

/// Blends two colors in linear color space.
/// Produces better results on both light and dark themes,
/// than integer blending (which is too dark).
QColor lerp_colors(QColor c1, QColor c2, qreal position) {
    #define GET_COLOR(I) \
        qreal r##I, g##I, b##I; \
        c##I.getRgbF(&r##I, &g##I, &b##I);

    GET_COLOR(1)
    GET_COLOR(2)

    #define BLEND_COLOR(CH) \
        sqrt(lerp(CH##1 * CH##1, CH##2 * CH##2, position))

    return QColor::fromRgbF(BLEND_COLOR(r), BLEND_COLOR(g), BLEND_COLOR(b));

    #undef BLEND_COLOR
}

QColor lerp_srgb(QColor c1, QColor c2, qreal position) {
    GET_COLOR(1)
    GET_COLOR(2)

    #define BLEND_SRGB(CH) \
        lerp(CH##1, CH##2, position)

    return QColor::fromRgbF(BLEND_SRGB(r), BLEND_SRGB(g), BLEND_SRGB(b));

    #undef BLEND_SRGB
}

}
