#pragma once

#include "gui/lib/color.h"
#include "util/constinit.h"
#include "util/release_assert.h"

#include <gsl/span>

#include <QColor>

#include <cmath>
#include <cstdint>
#include <type_traits>

namespace gui::lib::docs_palette {

//namespace Gray_ {
//enum Gray : uint32_t {
//    Black, Dark4, Dark3, Dark2, Dark1, Gray, Light1, Light2, Light3, White,
//    COUNT,
//    MAX = COUNT - 1,
//};
//}
//using Gray_::Gray;
//constexpr size_t GRAY_COUNT = Gray::COUNT;  // 10
//constexpr size_t GRAY_MAX = Gray::MAX;  // 9

namespace Shade_ {
enum Shade : uint32_t {
    Black,  // 0
    Dark3,  // 1
    Dark2,  // 2
    Dark1,  // 3
    Light1,  // 4
    Light2,  // 5
    Light3,  // 6
    White,  // 7
    COUNT,
    MAX = COUNT - 1,
};
}
using Shade_::Shade;
constexpr size_t SHADE_COUNT = Shade::COUNT;  // 8
constexpr size_t SHADE_MAX = Shade::MAX;  // 7

namespace Hue_ {
enum Hue : uint32_t {
    // Red Berry was omitted.
    Red, Orange, Yellow, Green, Cyan, Cornflower, Blue, Purple, Magenta,
    COUNT,
    MAX = COUNT - 1,
};
}
using Hue_::Hue;
constexpr size_t HUE_COUNT = Hue::COUNT;  // 9
constexpr size_t HUE_MAX = Hue::MAX;  // 8

namespace detail {
    extern QColor const GRAYS[SHADE_COUNT];
    extern QColor const COLORS[HUE_COUNT][SHADE_COUNT];
    extern QColor const PURE_COLORS[HUE_COUNT];

    using gui::lib::color::lerp_colors;

    constinit inline const QColor BLACK = QColor(0, 0, 0);
    constinit inline const QColor WHITE = QColor(255, 255, 255);

    template<typename Shade>
    QColor index_shade(gsl::span<QColor const, SHADE_COUNT> palette, Shade shade) {
        if constexpr (std::is_floating_point_v<Shade>) {
            // Catch NaN.
            if (shade != shade) {
                return BLACK;
            }
        }
        if (shade <= Shade(0)) {
            return BLACK;
        }
        // SHADE_MAX = SHADE_COUNT - 1, necessary to make floating point shades safe.
        if (shade >= Shade(palette.size() - 1)) {
            return WHITE;
        }

        if constexpr (std::is_integral_v<Shade> || std::is_same_v<Shade, Shade_::Shade>) {
            return palette[(size_t) shade];
        } else {
            static_assert(
                std::is_floating_point_v<Shade>,
                "invalid shade type, must be floating or Shade");
            size_t shade_floor = (size_t) std::floor(shade);
            auto shade_frac = shade - std::floor(shade);
            return lerp_colors(
                palette[shade_floor], palette[shade_floor + 1], shade_frac
            );
        }
    }

//    template<typename Hue, typename Shade>
//    constexpr QColor int_none(Hue hue, Shade shade) {
//        if constexpr (std::is_floating_point_v<Hue>) {
//            // Catch NaN.
//            if (hue != hue) {
//                return BLACK;
//            }
//        }

//        if constexpr (std::is_integral_v<Hue>) {
//            Hue modulo = hue % Hue(HUE_COUNT);
//            if constexpr (std::is_signed_v<Hue>) {
//                if (modulo < 0) {
//                    modulo += HUE_COUNT;
//                }
//            }
//            return int_hue((size_t) modulo, shade);
//        } else {
//            static_assert(
//                std::is_floating_point_v<Hue>,
//                "invalid hue type, must be integral or floating");
//            auto hue_floor = (ptrdiff_t) std::floor(hue) % (ptrdiff_t) HUE_COUNT;
//            if (hue_floor < 0) {
//                hue_floor += HUE_COUNT;
//            }

//            auto hue_next = ((size_t) hue_floor + 1) % HUE_COUNT;

//            auto hue_frac = hue - hue_floor;

//            return lerp_colors(
//                int_none(hue, hue_floor), int_none(hue, hue_next), hue_frac
//            );
//        }
//    }
}

QColor get_gray(auto shade) {
    return detail::index_shade(detail::GRAYS, shade);
}

template<typename Saturation = bool>
QColor get_color(Hue hue, auto shade, Saturation saturation = true) {
    using gui::lib::color::lerp_colors;

    release_assert(hue < HUE_COUNT);
    auto color = detail::index_shade(detail::COLORS[hue], shade);

    if constexpr (std::is_same_v<Saturation, bool>) {
        assert(saturation);
        return color;
    } else {
        static_assert(
            std::is_floating_point_v<Saturation>,
            "invalid saturation type, must be bool(true) or floating");
        auto gray = detail::index_shade(detail::GRAYS, shade);
        return lerp_colors(gray, color, saturation);
    }
}

} // namespace
