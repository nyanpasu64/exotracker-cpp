#pragma once

#include "doc/timeline.h"

#include <coroutine.h>

#include <tuple>

namespace doc_util::time_util {

using namespace doc::timeline;
using doc::event_list::EventIndex;

/// Timeline iterator that yields one pattern per loop instance.
class [[nodiscard]] FramePatternIter {
    scrDefine;

    BlockIndex _block;

    BeatIndex _loop_begin_time;
    BeatFraction _block_end_time;

    /// Where to truncate the event when looping.
    EventIndex _loop_ev_idx;

public:
    FramePatternIter() = default;

    /// You must pass in the same unmodified cell on each iteration,
    /// matching nbeats passed into the constructor.
    [[nodiscard]] MaybePatternRef next(TimelineCellRef cell_ref);
};

/// Version of FramePatternIter that holds onto a reference to the cell.
class [[nodiscard]] FramePatternIterRef {
    TimelineCellRef _cell_ref;
    FramePatternIter _iter;

public:
    FramePatternIterRef(TimelineCellRef cell_ref);

    [[nodiscard]] MaybePatternRef next();
};

// # Searching a document by time.

/// Returns a FramePatternIterRef pointing to a particular time in the document:
///
/// Returns the first (block, pattern loop) where pattern.end_time > beat,
/// or nullopt if the provided time lies after all patterns end.
std::tuple<MaybePatternRef, FramePatternIterRef> pattern_iter_seek(
    TimelineCellRef cell_ref, BeatFraction beat
);

/// Returns a PatternRef pointing to a particular time in the document:
///
/// Returns the first (block, pattern loop) where pattern.end_time > beat.
///
/// beat ∈ [prev_pattern.end_time, pattern.end_time).
/// If beat ≥ pattern.begin, beat ∈ [pattern.begin_time, pattern.end_time).
///
/// If there exists no pattern where pattern.end > beat,
/// returns (cell.size(), empty slice) which is out-of-bounds!
[[nodiscard]]
PatternRef pattern_or_end(TimelineCellRef cell_ref, BeatFraction beat);

} // namespaces

