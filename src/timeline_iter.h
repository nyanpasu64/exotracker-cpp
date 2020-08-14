#pragma once

/// Utility functions for searching through TimelineCell
/// and extracting patterns from Timeline.

#include "doc.h"
#include "timing_common.h"

#include <coroutine.h>

#include <tuple>

namespace timeline_iter {

using doc::ChipIndex;
using doc::ChannelIndex;

using doc::GridIndex;
using doc::BeatFraction;

using timing::GridAndBeat;
using timing::GridBlockBeat;
using timing::GridAndBlock;


// # Searching TimelineCell (only used in edit_pattern.cpp).

/// Returns the first (block, pattern loop) where pattern.end_time > beat.
///
/// beat ∈ [prev_pattern.end_time, pattern.end_time).
/// If beat ≥ pattern.begin, beat ∈ [pattern.begin_time, pattern.end_time).
///
/// If there exists no pattern where pattern.end > beat,
/// returns (cell.size(), empty slice) which is out-of-bounds!
[[nodiscard]]
doc::PatternRef pattern_or_end(
    doc::GridCell nbeats, doc::TimelineCell const& cell, BeatFraction beat
);


#if 0
/// Unused.
/// Convert a block-relative timestamp to a grid timestamp.
///
/// Adds the block's begin time and the relative timestamp's beat offset.
/// Then computes the correct grid cell containing the time.
[[nodiscard]] GridAndBeat real_time(
    doc::Document const& document,
    ChipIndex chip,
    ChannelIndex channel,
    GridBlockBeat rel_time
);
#endif


// # Iterating over Timeline (only used in move_cursor.cpp).

/// When moving the cursor around,
/// we need to compare whether the next event or row is closer to the cursor.
///
/// Wrapping from the end to the beginning of the document
/// is logically "later" than the end of the document,
/// and returns MoveCursorResult{Wrap::Plus, begin}.
/// This compares greater than MoveCursorResult{Wrap::Zero, end}.
enum class Wrap {
    Minus = -1,
    None = 0,
    Plus = +1,
};

struct BlockIteratorRef {
    Wrap wrapped{};
    GridIndex grid;
    doc::PatternRef pattern;
};

namespace detail {
    enum class Direction {
        Forward,
        Reverse,
    };

    /// Allocates memory, cannot be used on audio thread.
    ///
    /// Currently used for moving cursor to the next event
    /// (which may be on the current pattern, next, or even further).
    template<Direction direction>
    class BlockIterator {
    public:
        // Please don't poke this class's fields.
        // I'm marking them public to silence Clang warnings.
        scrDefine;
        doc::GridCells const& _grid_cells;
        doc::Timeline const& _timeline;

        GridIndex const _orig_grid;
        BeatFraction const _orig_pattern_start;

        int _wrap_count = 0;
        GridIndex _grid;
        std::vector<doc::PatternRef> _cell_patterns;
        size_t _pattern;

        // impl
    public:

        [[nodiscard]] static
        BlockIterator from_beat(
            doc::GridCells const& grid_cells, doc::Timeline const& timeline, GridAndBeat now
        );

        /// First call:
        /// - If original state is valid, return it as-is.
        /// - If original state is invalid, search the document for the first valid block.
        ///   If none exist, enter nullopt state.
        /// Subsequent calls:
        /// - Return the next block. Looping around the document is allowed.
        ///   When we loop back to the first block returned,
        ///   behavior is unspecified and will enter nullopt state at some point.
        /// Nullopt state:
        /// - Return nullopt.
        [[nodiscard]] std::optional<BlockIteratorRef> next();
    };
}

using ReverseBlockIterator = detail::BlockIterator<detail::Direction::Reverse>;
using ForwardBlockIterator = detail::BlockIterator<detail::Direction::Forward>;

#ifdef UNITTEST

#include <ostream>

static std::ostream& operator<< (std::ostream& os, Wrap const & value) {
    os << "Wrap(" << (int) value << ")";
    return os;
}

#endif

}
