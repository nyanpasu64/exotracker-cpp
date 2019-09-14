#pragma once

#include <boost/rational.hpp>
#include <cstdint>
#include <compare>

using Fraction = boost::rational<int64_t>;


struct TimeInPattern {
    Fraction anchor_beat;
    uint16_t frames_offset;
    auto operator<=>(TimeInPattern const &) const = default;
};
