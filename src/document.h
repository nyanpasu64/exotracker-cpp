#pragma once

/// Patterns contain rows at times (TimeInPattern).
/// TimeInPattern contains both a fractional anchor beat, and an offset in frames.
/// Rows can contain notes, effects, or both.
///
/// We use the C++ Immer library to implement immutable persistent data structures.
///
/// According to https://sinusoid.es/immer/containers.html#_CPPv2NK5immer6vectorixE9size_type ,
/// indexing into an Immer container returns a `const &`,
/// which becomes mutable when copied to a local.
/// Therefore when designing immer::type<Inner>, Inner can be a mutable struct,
/// std collection, or another immer::type.

#include <immer/flex_vector.hpp>
#include <immer/flex_vector_transient.hpp>
#include <boost/rational.hpp>
#include <cstdint>
#include <compare>
#include <optional>
#include <tuple>
#include <array>
#include <algorithm>

namespace doc {

using FractionInt = int64_t;
using BeatFraction = boost::rational<FractionInt>;


// TODO variant of u8 or note cut or etc.
using Note = uint8_t;

struct RowEvent {
    std::optional<Note> note;
    // TODO volumes and []effects

    bool operator==(RowEvent const &) const = default;
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

struct TimedChannelEvent {
    TimeInPattern time;
    RowEvent v;

    auto operator<=>(TimedChannelEvent const &) const = default;
};

using ChannelEvents = immer::flex_vector<TimedChannelEvent>;

/// Wrapper for ChannelEvents (I wish C++ had extension methods),
/// adding the ability to binary-search and treat it as a map.
template<typename ImmerT>
//using ImmerT = ChannelEvents::transient_type;
struct KV_Internal {
    using This = KV_Internal<ImmerT>;
//    using This = KV_Internal;

    ImmerT channel_events;

private:
    static bool cmp_less_than(TimedChannelEvent const &a, TimeInPattern const &b) {
        return a.time < b;
    }

    bool iter_matches_time(typename ImmerT::iterator iter, TimeInPattern t) const {
        if (iter == channel_events.end()) return false;
        if (iter->time != t) return false;
        return true;
    }

public:
    typename ImmerT::iterator greater_equal(TimeInPattern t) const {
        return std::lower_bound(channel_events.begin(), channel_events.end(), t, cmp_less_than);
    }

    bool contains_time(TimeInPattern t) const {
        // I cannot use std::binary_search,
        // since it requires that the comparator's arguments have interchangable types.
        return iter_matches_time(greater_equal(t), t);
    }

    std::optional<RowEvent> get_maybe(TimeInPattern t) const {
        auto iter = greater_equal(t);
        if (iter_matches_time(iter, t)) {
            return {iter->v};
        } else {
            return {};
        }
    }

    RowEvent get_or_default(TimeInPattern t) const {
        auto iter = greater_equal(t);
        if (iter_matches_time(iter, t)) {
            return iter->v;
        } else {
            return {};
        }
    }

    /// Only works with KV, not transient. This is because .insert() is missing on flex_vector_transient.
    This set_time(TimeInPattern t, RowEvent v) {
        auto iter = greater_equal(t);
        TimedChannelEvent timed_v{t, v};
        if (iter_matches_time(iter, t)) {
            return This{channel_events.set(iter.index(), timed_v)};
        } else {
            return This{channel_events.insert(iter.index(), timed_v)};
        }
    }
};

using KV = KV_Internal<ChannelEvents>;
using KVTransient = KV_Internal<ChannelEvents::transient_type>;

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

///// get_document() must be thread-safe in implementations.
///// For example, if implemented by DocumentHistory,
///// get_document() must not return invalid states while undoing/redoing.
//class GetDocument {
//public:
//    virtual ~GetDocument() = default;
//    virtual TrackPattern const & get_document() const = 0;
//};


// namespace doc
}
