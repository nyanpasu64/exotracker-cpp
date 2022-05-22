#pragma once

#include "doc/timeline.h"

namespace doc_util::track_util {

using namespace doc::timeline;

// Cross-track search

TickT song_length(Sequence const& tracks);

// Per-track pattern iteration

struct IterResult;

/// Track iterator that yields one pattern per loop instance.
///
/// You must pass in the same unmodified track to every method call.
class [[nodiscard]] TrackPatternIter {
    // Mutable
    /// Normally in [0..blocks.size()). When incremented past the document, equals
    /// blocks.size(). When decremented before the document, equals (uint32_t) -1.
    ///
    /// Bad things happen when you try to make a class a forward and reverse iterator
    /// at the same time. But the exposed API is sooo convenient...
    BlockIndex _maybe_block_idx;
    uint32_t _loop_idx;

private:
    TrackPatternIter(BlockIndex block_idx, uint32_t loop_idx);

public:
    /// Find the first block where now < block.end, and save the first loop index where
    /// now < loop.end.
    static IterResult at_time(SequenceTrackRef track, TickT now);

    [[nodiscard]] MaybePatternRef peek(SequenceTrackRef track) const;

    /// Do not call if _block_idx == {-1 or blocks.size()}.
    void next(SequenceTrackRef track);

    /// Do not call if _block_idx == -1. Safe to call on blocks.size().
    void prev(SequenceTrackRef track);
};

struct IterResult {
    TrackPatternIter iter;
    bool snapped_later;
};

struct IterResultRef;

/// Version of TrackPatternIter that holds onto a reference to the cell.
class [[nodiscard]] TrackPatternIterRef {
    SequenceTrack const* _track;
    TrackPatternIter _iter;

private:
    TrackPatternIterRef(SequenceTrackRef track, TrackPatternIter iter);

public:
    /// Find the first block where now < block.end, and save the first loop index where
    /// now < loop.end.
    static IterResultRef at_time(SequenceTrackRef track, TickT now);

    [[nodiscard]] MaybePatternRef peek() const;

    /// Do not call if _block_idx == {-1 or blocks.size()}.
    void next();

    /// Do not call if _block_idx == -1. Safe to call on blocks.size().
    void prev();
};

struct IterResultRef {
    TrackPatternIterRef iter;
    bool snapped_later;
};

} // namespace

