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

#include "audio/synth/chip_kinds_common.h"

#include <immer/array.hpp>
#include <immer/array_transient.hpp>
#include <immer/flex_vector.hpp>
#include <immer/flex_vector_transient.hpp>
#include <boost/rational.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <tuple>
#include <utility>

namespace doc {

namespace chip_kinds = audio::synth::chip_kinds;

// Adapted from https://www.fluentcpp.com/2019/04/09/how-to-emulate-the-spaceship-operator-before-c20-with-crtp/

#define KEY(method, paren_of_fields) \
    constexpr auto method() const { \
        return std::tie paren_of_fields; \
    } \

#define COMPARE_ONLY(method, T) \
    [[nodiscard]] constexpr bool operator<(const T& other) const { \
        return this->method() < other.method(); \
    } \
    [[nodiscard]] constexpr bool operator>(const T& other) const { \
        return this->method() > other.method(); \
    } \
    [[nodiscard]] constexpr bool operator>=(const T& other) const { \
        return this->method() >= other.method(); \
    } \
    [[nodiscard]] constexpr bool operator<=(const T& other) const { \
        return this->method() <= other.method(); \
    } \

#define EQUALABLE(method, T) \
    [[nodiscard]] constexpr bool operator==(const T& other) const { \
        return this->method() == other.method(); \
    } \
    [[nodiscard]] constexpr bool operator!=(const T& other) const { \
        return this->method() != other.method(); \
    } \


#define COMPARABLE(method, T) \
    EQUALABLE(method, T) \
    COMPARE_ONLY(method, T) \

using FractionInt = int64_t;
using BeatFraction = boost::rational<FractionInt>;

// TODO variant of u8 or note cut or etc.
using Note = uint8_t;

struct RowEvent {
    std::optional<Note> note;
    // TODO volumes and []effects

    KEY(key_, (note))
    EQUALABLE(key_, RowEvent)
};

/// A timestamp of a row in a pattern.
///
/// Everything about exotracker operates using half-open [inclusive, exclusive) ranges.
/// begin_of_beat() makes it easy to find all notes whose anchor_beat lies in [a, b).
///
/// `anchor_beat` controls "how many beats into the pattern" the note plays.
/// It should be non-negative.
///
/// The NES generally runs the audio driver 60 times a second.
/// Negative or positive `frames_offset` causes a note to play before or after the beat.
///
/// All positions are sorted by (anchor_beat, frames_offset).
/// This code makes no attempt to prevent `frames_offset` from
/// causing the sorting order to differ from the playback order.
/// If this happens, the pattern is valid, but playing the pattern will misbehave.
struct TimeInPattern {
    BeatFraction anchor_beat;
    int16_t frames_offset;

    KEY(key_, (anchor_beat, frames_offset))
    COMPARABLE(key_, TimeInPattern)

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

    KEY(equal_, (time, v))
    EQUALABLE(equal_, TimedChannelEvent)

    KEY(compare_, (time))
    COMPARE_ONLY(compare_, TimedChannelEvent)
};

using EventList = immer::flex_vector<TimedChannelEvent>;

/// Owning wrapper for EventList (I wish C++ had extension methods),
/// adding the ability to binary-search and treat it as a map.
template<typename ImmerT>
struct KV_Internal {
    using This = KV_Internal<ImmerT>;

    ImmerT event_list;

private:
    static bool cmp_less_than(TimedChannelEvent const &a, TimeInPattern const &b) {
        return a.time < b;
    }

    bool iter_matches_time(typename ImmerT::iterator iter, TimeInPattern t) const {
        if (iter == event_list.end()) return false;
        if (iter->time != t) return false;
        return true;
    }

public:
    typename ImmerT::iterator greater_equal(TimeInPattern t) const {
        return std::lower_bound(event_list.begin(), event_list.end(), t, cmp_less_than);
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
            return This{event_list.set(iter.index(), timed_v)};
        } else {
            return This{event_list.insert(iter.index(), timed_v)};
        }
    }
};

using KV = KV_Internal<EventList>;
using KVTransient = KV_Internal<EventList::transient_type>;

struct SequenceEntry {
    BeatFraction nbeats;

    // TODO add pattern indexing scheme.
    /**
    Invariant (expressed through dependent types):
    - [chip: ChipInt] [ChannelID<chips[chip]: ChipKind>] EventList

    Invariant (expressed without dependent types):
    - chip: (ChipInt = [0, Document.chips.size()))
    - chips[chip]: ChipKind
    - channel: (ChannelIndex = [0, CHIP_TO_NCHAN[chip]))
    - chip_channel_events[chip][channel]: EventList
    */
    using ChannelToEvents = immer::array<EventList>;
    using ChipChannelEvents = immer::array<ChannelToEvents>;
    ChipChannelEvents chip_channel_events;
};

using FlatChannelInt = uint32_t;

struct Document {
    /// vector<ChipIndex -> ChipKind>
    /// chips.size() in [1..MAX_NCHIP] inclusive (not enforced yet).
    using ChipList = immer::array<chip_kinds::ChipKind>;
    ChipList chips;

    chip_kinds::ChannelIndex chip_index_to_nchan(chip_kinds::ChipIndex index) const {
        return chip_kinds::CHIP_TO_NCHAN[chips[index]];
    }

    // TODO add multiple patterns.
    SequenceEntry pattern;
};

struct HistoryFrame {
    Document document;
    // TODO add std::string diff_from_previous.
};

/// get_document() must be thread-safe in implementations.
/// For example, if implemented by DocumentHistory,
/// get_document() must not return invalid states while undoing/redoing.
class GetDocument {
public:
    virtual ~GetDocument() = default;
    virtual Document const & get_document() const = 0;
};

// immer::flex_vector (possibly other types)
// is a class wrapping immer/detail/rbts/rrbtree.hpp.
// immer's rrbtree is the size of a few pointers, and does not hold node data.
// So immer types take up little space in their owning struct (comparable to shared_ptr).

// namespace doc
}
