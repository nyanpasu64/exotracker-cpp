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
#include "util/compare.h"

#include <immer/array.hpp>
#include <immer/array_transient.hpp>
#include <immer/flex_vector.hpp>
#include <immer/flex_vector_transient.hpp>
#include <boost/rational.hpp>
#include <gsl/span>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>

namespace doc {

namespace chip_kinds = audio::synth::chip_kinds;

using FractionInt = int64_t;
using BeatFraction = boost::rational<FractionInt>;

template <typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}

template <typename rational>
inline int round_to_int(rational v)
{
    v = v + typename rational::int_type(sgn(v.numerator())) / 2;
    return boost::rational_cast<int>(v);
}

inline namespace note {
    using ChromaticInt = int16_t;
    constexpr int CHROMATIC_COUNT = 128;

    struct Note {
        ChromaticInt value;

        // Implicit conversion constructor.
        // Primarily here for gui::history::dummy_document().
        constexpr Note(ChromaticInt value) : value(value) {}

        // impl
        EQUALABLE(Note, (value))

        [[nodiscard]] constexpr bool is_cut() const;
        [[nodiscard]] constexpr bool is_release() const;

        /// Returns true if note.value is an in-bounds array index,
        /// not a cut/release, negative value, or out-of-bounds index.
        [[nodiscard]] constexpr bool is_valid_note() const;
    };

    constexpr Note NOTE_CUT{-1};
    constexpr Note NOTE_RELEASE{-2};

    constexpr bool Note::is_cut() const {
        return *this == NOTE_CUT;
    }

    constexpr bool Note::is_release() const {
        return *this == NOTE_RELEASE;
    }

    constexpr bool Note::is_valid_note() const {
        return 0 <= value && (size_t) (value) < CHROMATIC_COUNT;
    }
}

struct RowEvent {
    std::optional<Note> note;
    // TODO volumes and []effects

    EQUALABLE(RowEvent, (note))
};

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
    RowEvent v;

    EQUALABLE(TimedRowEvent, (time, v))
    COMPARE_ONLY(TimedRowEvent, (time))
};

/// Pattern type.
using EventList = immer::flex_vector<TimedRowEvent>;

/// Owning wrapper for EventList (I wish C++ had extension methods),
/// adding the ability to binary-search and treat it as a map.
template<typename ImmerT>
struct KV_Internal {
    using This = KV_Internal<ImmerT>;

    ImmerT event_list;

private:
    static bool cmp_less_than(TimedRowEvent const &a, TimeInPattern const &b) {
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
        TimedRowEvent timed_v{t, v};
        if (iter_matches_time(iter, t)) {
            return This{event_list.set(iter.index(), timed_v)};
        } else {
            return This{event_list.insert(iter.index(), timed_v)};
        }
    }
};

using KV = KV_Internal<EventList>;
using KVTransient = KV_Internal<EventList::transient_type>;

/// Semantic typedef around a runtime-sized vector.
/// K is an integer type type-erased without type-checking,
/// and exists to document the domain space.
/// It's semantically nonsensical to index a list of sound chips
/// with a pixel coordinate integer.
template<typename K, typename V>
using DenseMap = immer::array<V>;

using chip_kinds::ChipIndex;
using chip_kinds::ChannelIndex;

template<typename V>
using ChipChannelTo = DenseMap<ChipIndex, DenseMap<ChannelIndex, V>>;

template<typename V>
using ChannelTo = DenseMap<ChannelIndex, V>;

/// Represents the contents of one row in the sequence editor.
/// Like FamiTracker, Exotracker will use a pattern system
/// where each sequence row contains [for each channel] pattern ID.
///
/// The list of [chip/channel] [pattern ID] -> pattern data is stored separately
/// (in PatternStore).
struct SequenceEntry {
    /// Invariant: Must be positive and nonzero.
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
    ChipChannelTo<EventList> chip_channel_events;
};

struct SequencerOptions {
    TickT ticks_per_beat;
};

// Tuning table types
inline namespace tuning {
    using ChromaticInt = ChromaticInt;
    using FreqDouble = double;
    using RegisterInt = int;

    template<typename T>
    using Owned_ = std::array<T, doc::CHROMATIC_COUNT>;

    template<typename T>
    using Ref_ = gsl::span<T const, doc::CHROMATIC_COUNT>;

    using FrequenciesOwned = Owned_<FreqDouble>;
    using FrequenciesRef = Ref_<FreqDouble>;

    using TuningOwned = Owned_<RegisterInt>;
    using TuningRef = Ref_<RegisterInt>;

    FrequenciesOwned equal_temperament(
        ChromaticInt root_chromatic = 69, FreqDouble root_frequency = 440.
    );
}

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

    SequencerOptions sequencer_options;
    FrequenciesOwned frequency_table;
};

Document dummy_document();

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
