#pragma once

#include "events.h"
#include "util/compare.h"

#include <boost/rational.hpp>

#include <limits>

namespace doc::timed_events {

using FractionInt = int32_t;
using BeatFraction = boost::rational<FractionInt>;

template <typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

/// Used to round beat-fraction timestamps to integer ticks.
template <typename rational>
inline int round_to_int(rational v)
{
    v = v + typename rational::int_type(sgn(v.numerator())) / 2;
    return boost::rational_cast<int>(v);
}


/// Why signed? Events can have negative offsets and play before their anchor beat,
/// or even before the owning pattern starts. This is a feature(tm).
using TickT = int32_t;

/// A timestamp of a row in a pattern.
///
/// Everything about exotracker operates using half-open [inclusive, exclusive) ranges.
/// begin_of_beat() makes it easy to find all notes whose anchor_beat lies in [a, b).
///
/// `anchor_beat` controls "how many beats into the pattern" the note plays.
/// It should be non-negative.
///
/// The NES generally runs the audio driver 60 times a second.
/// Negative or positive `tick_offset` causes a note to play before or after the beat.
///
/// All positions are sorted by (anchor_beat, tick_offset).
/// This code makes no attempt to prevent `tick_offset` from
/// causing the sorting order to differ from the playback order.
/// If this happens, the pattern is valid, but playing the pattern will misbehave.
struct TimeInPattern {
    BeatFraction anchor_beat;
    TickT tick_offset;
    using TickLimits = std::numeric_limits<decltype(tick_offset)>;

    COMPARABLE(TimeInPattern, (anchor_beat, tick_offset))

    // TODO remove, only used for testing purposes
    static TimeInPattern from_frac(FractionInt num, FractionInt den) {
        return {{num, den}, 0};
    }

    /// A timestamp which lies before any notes anchored to the current beat.
    TimeInPattern begin_of_beat() const {
        return {this->anchor_beat, TickLimits::min()};
    }

    /// A timestamp which lies before any notes anchored to the given beat.
    static TimeInPattern begin_of_beat(BeatFraction anchor_beat) {
        return {anchor_beat, TickLimits::min()};
    }
};

struct TimedRowEvent {
    TimeInPattern time;
    events::RowEvent v;

    EQUALABLE(TimedRowEvent, (time, v))
    COMPARE_ONLY(TimedRowEvent, (time))
};

// end namespace
}
