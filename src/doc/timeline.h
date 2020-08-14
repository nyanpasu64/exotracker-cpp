#pragma once

/// See DESIGN.md for details on the timeline system.

#include "event_list.h"
#include "timed_events.h"
#include "doc_common.h"  // DenseMap
#include "chip_common.h"
#include "util/compare.h"
#include "util/safe_typedef.h"

#include <coroutine.h>
#include <gsl/span>

#include <compare>
#include <optional>

namespace doc::timeline {

using chip_common::ChipIndex;
using chip_common::ChannelIndex;

template<typename V>
using ChipChannelTo = DenseMap<ChipIndex, DenseMap<ChannelIndex, V>>;

template<typename V>
using ChannelTo = DenseMap<ChannelIndex, V>;

struct GridIndex {
    EXPLICIT_TYPEDEF(uint32_t, GridIndex)
};
using MaybeGridIndex = std::optional<GridIndex>;

struct BlockIndex {
    EXPLICIT_TYPEDEF(uint32_t, BlockIndex)
};
using MaybeBlockIndex = std::optional<BlockIndex>;

/// i tried
template<typename T>
using MaybeNonZero = T;


/// The length of a pattern is determined by its entry in the timeline (PatternUsage).
/// However a Pattern may specify a loop length.
/// If set, the first `loop_length` beats of the Pattern will loop
/// for the duration of the PatternUsage.
struct Pattern {
    event_list::EventList events;

    /// Loop length in beats. If length is zero, don't loop the pattern.
    MaybeNonZero<uint32_t> loop_length{};

    #ifdef UNITTEST
    DEFAULT_EQUALABLE(Pattern)
    #endif
};


using timed_events::BeatFraction;

using BeatIndex = timed_events::FractionInt;
struct BeatOrEnd {
    EXPLICIT_TYPEDEF(uint32_t, BeatOrEnd)

    template<typename T>
    T value_or(T other) const;
};

constexpr BeatOrEnd END_OF_GRID = (BeatOrEnd) -1;

template<typename T>
T BeatOrEnd::value_or(T other) const {
    if (*this == END_OF_GRID) return other;
    return this->v;
}


inline std::strong_ordering operator<=>(BeatOrEnd l, BeatFraction r) {
    using Ord = std::strong_ordering;
    using timed_events::FractionInt;

    if (l == END_OF_GRID) {
        return Ord::greater;
    }

    auto ll = FractionInt(l);
    if (ll > r) return Ord::greater;
    if (ll < r) return Ord::less;
    return Ord::equal;
}

/// Each pattern usage in the timeline has a begin and end time.
/// To match traditional trackers, these times can align with the global pattern grid.
/// But you can get creative and offset the pattern by an integer number of beats.
/// (Maybe I'll allow negative offsets too.)
///
/// It is legal to have gaps between `PatternUsage` in the timeline
/// where no events are processed.
/// It is illegal for `PatternUsage` to overlap in the timeline.
struct TimelineBlock {
    /// Invariant: begin_time < end_time
    /// (cannot be equal, since it becomes impossible to select the usage).
    ///
    /// Invariant: TimelineBlock cannot cross gridlines.
    /// Long patterns crossing multiple gridlines makes it difficult to compute
    /// the relative time within a pattern when seeking to a (grid, beat) timestamp.
    BeatIndex begin_time{};
    BeatOrEnd end_time{};

    /// For now, `TimelineBlock` owns a `Pattern`.
    /// Eventually it should store a `PatternID` indexing into an
    /// (either global or per-channel) store of shared patterns.
    /// Or maybe a variant of these two.
    Pattern pattern;

    #ifdef UNITTEST
    DEFAULT_EQUALABLE(TimelineBlock)
    #endif
};


/// One channel, one grid cell, can hold multiple blocks at non-overlapping times.
struct TimelineCell {
    DenseMap<BlockIndex, TimelineBlock> _raw_blocks;

    // impl
    // Implicit conversion constructor to simplify sample_docs.cpp.
    TimelineCell(std::initializer_list<TimelineBlock> l) : _raw_blocks(l) {}

    #ifdef UNITTEST
    DEFAULT_EQUALABLE(TimelineCell)
    #endif

    [[nodiscard]] BlockIndex size() const {
        return (BlockIndex) _raw_blocks.size();
    }
};


/// One channel, whole song.
using Timeline = DenseMap<GridIndex, TimelineCell>;


/// Type doesn't really matter.
///
/// Not sure what MAX_SEQUENCE_LEN means.
/// Maybe it needs to be split into grid cell limit, pattern usage limit,
/// and pattern limit.
constexpr int MAX_GRID_CELLS = 256;


/// Not strictly enforced. But exceeding this could cause problems
/// with the hardware driver, or skips in the audio.
constexpr int MAX_BLOCKS_PER_CELL = 32;

struct GridCell {
    BeatFraction nbeats;
};

using GridCells = DenseMap<GridIndex, GridCell>;


// # Iterating over looped patterns within blocks in a timeline

using event_list::TimedEventsRef;

/// A pattern can be played partially (short block with a long pattern)
/// or multiple times (long block with a short looped pattern).
/// Each PatternLoop points to part of a pattern, played at a specific real time.
///
/// PatternLoop can be constructed from a TimelineBlock/Pattern
/// without allocating memory, allowing it to be used on the audio thread.
struct PatternRef {
    BlockIndex block;
    // int loop;

    /// Timestamps within the current grid cell.
    BeatIndex begin_time{};
    BeatFraction end_time{};

    /// True if this is the first loop.
    bool is_block_begin = true;
    /// True if this is the last loop.
    bool is_block_end = true;

    /// Events carrying timestamps relative to begin_time.
    TimedEventsRef events{};
};

using MaybePatternRef = std::optional<PatternRef>;

/// Timeline iterator that yields one pattern per loop instance.
class [[nodiscard]] TimelineCellIter {
    scrDefine;

    /// Do not overwrite this field in any method (only via assignment).
    BeatFraction _cell_nbeats;

    BeatIndex _loop_begin_time;

    BlockIndex _block;

    /// Where to truncate the event when looping.
    event_list::EventIndex _loop_ev_idx;

public:
    TimelineCellIter(GridCell nbeats);

    /// You must pass in the same unmodified cell on each iteration,
    /// matching nbeats passed into the constructor.
    [[nodiscard]] MaybePatternRef next(TimelineCell const& cell);
};

/// Version of TimelineCellIter that holds onto a reference to the cell.
class [[nodiscard]] TimelineCellIterRef {
    TimelineCell const& _cell;
    TimelineCellIter _iter;

public:
    TimelineCellIterRef(TimelineCell const& cell, GridCell nbeats);

    [[nodiscard]] MaybePatternRef next();
};

}
