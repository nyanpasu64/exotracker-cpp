#pragma once

/// Patterns contain rows at times (TimeInPattern).
/// TimeInPattern contains both a fractional anchor beat, and an offset in frames.
/// Rows can contain notes, effects, or both.

#include <boost/rational.hpp>
#include <cstdint>
#include <compare>
#include <map>
#include <optional>
#include <tuple>
#include <array>

namespace document {

using FractionInt = int64_t;
using BeatFraction = boost::rational<FractionInt>;


// TODO variant of u8 or note cut or etc.
using Note = uint8_t;

struct RowEvent {
    std::optional<Note> note;
    // TODO volumes and []effects

    auto operator<=>(RowEvent const &) const = default;
};

/// A timestamp of a row in a pattern.
///
/// Everything about exotracker operates using half-open [inclusive, exclusive) ranges.
/// begin_of_beat() makes it easy to find all notes whose anchor_beat lies in [a, b).
///
/// `anchor_beat` controls "how many beats into the pattern" the note plays.
/// It should be non-negative.
///
/// The NES generally runs the audio engine 60 times a second.
/// Negative or positive `frames_offset` causes a note to play before or after the beat.
///
/// All positions are sorted by (anchor_beat, frames_offset).
/// This code makes no attempt to prevent `frames_offset` from
/// causing the sorting order to differ from the playback order.
/// If this happens, the pattern is valid, but playing the pattern will misbehave.
struct TimeInPattern {
    BeatFraction anchor_beat;
    int16_t frames_offset;
    std::strong_ordering operator<=>(TimeInPattern const & rhs) const {
        // msvc 2019 defines an implicit `operator<=>` on `boost::rational` which behaves in a nonsensical fashion
        if (anchor_beat < rhs.anchor_beat) return std::strong_ordering::less;
        if (anchor_beat > rhs.anchor_beat) return std::strong_ordering::greater;
        return frames_offset <=> rhs.frames_offset;
    }

    auto operator==(TimeInPattern const & rhs) const {
        return anchor_beat == rhs.anchor_beat && frames_offset == rhs.frames_offset;
    }

    // TODO remove, only used for testing purposes
    static TimeInPattern from_frac(FractionInt num, FractionInt den) {
        return {{num, den}, 0};
    }

    /// A timestamp which lies before any notes anchored to the current beat.
    TimeInPattern begin_of_beat() const {
        return {this->anchor_beat, INT16_MIN};
    }

    /// A timestamp which lies before any notes anchored to the given beat.
    static TimeInPattern begin_of_beat(BeatFraction anchor_beat) {
        return {anchor_beat, INT16_MIN};
    }
};

using ChannelEvents = std::map<TimeInPattern, RowEvent>;

namespace _ChannelId {
enum ChannelId {
    Test1,
    Test2,
    COUNT
};
}
using _ChannelId::ChannelId;
using ChannelInt = int;

struct TrackPattern {
    BeatFraction nbeats;
    std::array<ChannelEvents, ChannelId::COUNT> channels;
};


// namespace
}
