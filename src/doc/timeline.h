#pragma once

/// See DESIGN.md for details on the timeline system.

#include "event_list.h"
#include "timed_events.h"
#include "doc_common.h"  // DenseMap
#include "chip_common.h"
#include "util/compare.h"
#include "util/safe_typedef.h"

#ifdef UNITTEST
#include "util/compare.h"
#endif

#include <gsl/span>

#include <compare>
#include <optional>

namespace doc::timeline {

// # Indexing and bounds

struct BlockIndex {
    EXPLICIT_TYPEDEF(uint32_t, BlockIndex)
};
using MaybeBlockIndex = std::optional<BlockIndex>;

/// Not strictly enforced. But exceeding this could cause problems
/// with the hardware driver, or skips in the audio.
///
/// BlockIndex < SequenceTrack.blocks.size() <= MAX_BLOCKS_PER_TRACK.
constexpr size_t MAX_BLOCKS_PER_TRACK = 1024;


// # Utility types

using chip_common::ChipIndex;
using chip_common::ChannelIndex;

template<typename V>
using ChipChannelTo = DenseMap<ChipIndex, DenseMap<ChannelIndex, V>>;


// # Sub-grid pattern types

using timed_events::TickT;

/// Loose limit on the maximum length of a song.
constexpr TickT MAX_TICK = (1 << 30) - 1;

/// A pattern holds a list of events. It also determines its own duration, while the
/// block holding it (or in the future each block referencing its ID) determines how
/// many times to loop it.
struct [[nodiscard]] Pattern {
    TickT length_ticks;

    event_list::EventList events;

// impl
#ifdef UNITTEST
    DEFAULT_EQUALABLE(Pattern)
#endif
};


constexpr int MAX_BEATS_PER_MEASURE = 128;


/// Each block (pattern usage) in a track has a begin time and loop count, and
/// references a pattern which stores its own length. Blocks can be placed at arbitrary
/// ticks, like AMK but unlike frame-based trackers.
///
/// It is legal to have gaps between `TrackBlock` in a track where no events are
/// processed. It is illegal for `TrackBlock` to overlap in a track.
struct [[nodiscard]] TrackBlock {
    /// Invariant: begin_time < end_time
    /// (cannot be equal, since it becomes impossible to select the usage).
    ///
    /// Invariant: TrackBlock cannot cross gridlines.
    /// Long patterns crossing multiple gridlines makes it difficult to compute
    /// the relative time within a pattern when seeking to a (grid, beat) timestamp.
    TickT begin_tick{};

    uint32_t loop_count = 1;

    /// For now, `TrackBlock` owns a `Pattern`.
    /// Eventually it should store a `PatternID` indexing into an
    /// (either global or per-channel) store of shared patterns.
    /// Or maybe a variant of these two.
    Pattern pattern;

// impl
    static TrackBlock from_events(
        TickT begin_tick,
        TickT length_ticks,
        event_list::EventList events,
        uint32_t loop_count = 1)
    {
        return TrackBlock {
            .begin_tick = begin_tick,
            .loop_count = loop_count,
            .pattern = Pattern {
                .length_ticks = length_ticks,
                .events = std::move(events),
            },
        };
    }

#ifdef UNITTEST
    DEFAULT_EQUALABLE(TrackBlock)
#endif
};


// # Track types

struct ChannelSettings {
    events::EffColIndex n_effect_col = 1;

// impl
#ifdef UNITTEST
    DEFAULT_EQUALABLE(ChannelSettings)
#endif
};

/// One channel. Can hold multiple blocks at non-overlapping increasing times. Each
/// block should have nonzero length (zero-length blocks may break editing or the
/// sequencer). Notes are cut upon each block end, to match AMK.
struct [[nodiscard]] SequenceTrack {
    DenseMap<BlockIndex, TrackBlock> blocks;
    ChannelSettings settings{};

// impl
    // Implicit conversion constructor to simplify sample_docs.cpp.
    SequenceTrack(std::initializer_list<TrackBlock> l, ChannelSettings settings = {})
        : blocks(l)
        , settings(settings)
    {}

    SequenceTrack(std::vector<TrackBlock> raw_blocks, ChannelSettings settings = {})
        : blocks(std::move(raw_blocks))
        , settings(settings)
    {}

    SequenceTrack() = default;

#ifdef UNITTEST
    DEFAULT_EQUALABLE(SequenceTrack)
#endif
};

using ChipChannelTracks = ChipChannelTo<SequenceTrack>;
using Sequence = ChipChannelTracks;


using SequenceTrackRef = SequenceTrack const&;
using SequenceTrackRefMut = SequenceTrack &;


// # Iterating over looped patterns within blocks in a track

using event_list::TimedEventsRef;

/// A pattern can be played multiple times in a song, when a block loops a pattern (or
/// eventually when multiple blocks reference the same pattern). Each PatternRef
/// points to a pattern being played at a specific absolute time.
///
/// PatternRef can be constructed from a TrackBlock/Pattern
/// without allocating memory, allowing it to be used on the audio thread.
struct PatternRef {
    BlockIndex block;

    /// Timestamps within document.
    TickT begin_tick{};
    TickT end_tick{};

    /// True if this is the first loop.
    bool is_block_begin = true;
    /// True if this is the last loop.
    bool is_block_end = true;

    /// Events carrying timestamps relative to begin_time.
    TimedEventsRef events{};
};

using MaybePatternRef = std::optional<PatternRef>;

}
