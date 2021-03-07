#include "color.h"

#include <cmath>  // pow

namespace gui::lib::color {

QColor lerp_colors(QColor c1, QColor c2, float position) {
    #define GET_COLOR(I) \
        float r##I, g##I, b##I; \
        c##I.getRgbF(&r##I, &g##I, &b##I);

    GET_COLOR(1)
    GET_COLOR(2)

    #define BLEND_COLOR(CH) \
        sqrtf(lerp(CH##1 * CH##1, CH##2 * CH##2, position))

    return QColor::fromRgbF(BLEND_COLOR(r), BLEND_COLOR(g), BLEND_COLOR(b));

    #undef BLEND_COLOR
}

QColor lerp_srgb(QColor c1, QColor c2, float position) {
    GET_COLOR(1)
    GET_COLOR(2)

    #define BLEND_SRGB(CH) \
        lerp(CH##1, CH##2, position)

    return QColor::fromRgbF(BLEND_SRGB(r), BLEND_SRGB(g), BLEND_SRGB(b));

    #undef BLEND_SRGB
}

}
