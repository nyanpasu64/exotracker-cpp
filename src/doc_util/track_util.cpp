#include "track_util.h"
#include "util/release_assert.h"

#include <algorithm>  // std::max, std::*_bound

namespace doc_util::track_util {

// Cross-track search

static TickT end_time(TrackBlock const& block) {
    return block.begin_tick + (int) block.loop_count * block.pattern.length_ticks;
}

static TickT loop_time(TrackBlock const& block, uint32_t loop_idx) {
    return block.begin_tick + (int) loop_idx * block.pattern.length_ticks;
}

TickT song_length(Sequence const& tracks) {
    TickT max_len = 0;
    for (auto const& channel_tracks : tracks) {
        for (SequenceTrack const& track : channel_tracks) {
            auto const& blocks = track.blocks;
            if (!blocks.empty()) {
                max_len = std::max(max_len, end_time(blocks.back()));
            }
        }
    }
    return max_len;
}

// Per-track pattern iteration

TrackPatternIter::TrackPatternIter(BlockIndex block_idx, uint32_t loop_idx)
    : _maybe_block_idx(block_idx)
    , _loop_idx(loop_idx)
{}

IterResult TrackPatternIter::at_time(SequenceTrackRef track, TickT now) {
    auto const& blocks = track.blocks;

    // Find the first block where now < block.end.
    auto block_at_cursor = std::upper_bound(
        blocks.begin(), blocks.end(),
        now,
        [](TickT now, TrackBlock const& block) {
            return now < end_time(block);
        });

    const auto block_idx = (BlockIndex) (block_at_cursor - blocks.begin());

    if (block_at_cursor == blocks.end()) {
        return IterResult {
            .iter = TrackPatternIter(block_idx, 0),
            .snapped_later = true,
        };
    } else {
        TrackBlock const& block = *block_at_cursor;
        assert(now < end_time(block));

        const bool snapped_later = now < block.begin_tick;
        uint32_t loop_idx = snapped_later
            ? 0
            : (uint32_t) ((now - block.begin_tick) / block.pattern.length_ticks);
        return IterResult {
            .iter = TrackPatternIter(block_idx, loop_idx),
            .snapped_later = snapped_later,
        };
    }
}

MaybePatternRef TrackPatternIter::peek(SequenceTrackRef track) const {
    auto const& blocks = track.blocks;

    if (_maybe_block_idx.v < blocks.size()) {
        TrackBlock const& block = blocks[_maybe_block_idx];

        return PatternRef {
            .block = _maybe_block_idx,
            .begin_tick = loop_time(block, _loop_idx),
            .end_tick = loop_time(block, _loop_idx + 1),
            .is_block_begin = _loop_idx == 0,
            .is_block_end = _loop_idx + 1 == block.loop_count,
            .events = TimedEventsRef(block.pattern.events)
        };
    } else {
        return {};
    }
}

void TrackPatternIter::next(SequenceTrackRef track) {
    auto const& blocks = track.blocks;
    release_assert(_maybe_block_idx.v < blocks.size());
    TrackBlock const& block = blocks[_maybe_block_idx];

    if (_loop_idx + 1 < block.loop_count) {
        _loop_idx++;
    } else {
        _loop_idx = 0;
        _maybe_block_idx++;
    }
}

void TrackPatternIter::prev(SequenceTrackRef track) {
    auto const& blocks = track.blocks;
    release_assert(_maybe_block_idx.v != (uint32_t) -1);
    if (_loop_idx == 0) {
        if (_maybe_block_idx--) {  // May wrap; this is sound.
            TrackBlock const& block = blocks[_maybe_block_idx];
            _loop_idx = block.loop_count - 1;
        }
    } else {
        _loop_idx--;
    }
}

TrackPatternIterRef::TrackPatternIterRef(SequenceTrackRef track, TrackPatternIter iter)
    : _track(&track)
    , _iter(iter)
{}

IterResultRef TrackPatternIterRef::at_time(SequenceTrackRef track, TickT now) {
    auto [iter, snapped_later] = TrackPatternIter::at_time(track, now);
    return IterResultRef {
        .iter = TrackPatternIterRef(track, iter),
        .snapped_later = snapped_later,
    };
}

MaybePatternRef TrackPatternIterRef::peek() const {
    return _iter.peek(*_track);
}

void TrackPatternIterRef::next() {
    _iter.next(*_track);
}

void TrackPatternIterRef::prev() {
    _iter.prev(*_track);
}

} // namespace

#ifdef UNITTEST

#include "doc_util/event_builder.h"
#include "util/compare.h"
#include "util/compare_impl.h"
#include "util/enumerate.h"

#include <doctest.h>

namespace doc_util::track_util {
TEST_SUITE_BEGIN("doc_util/track_util");

using namespace doc::events;
using namespace doc::timed_events;
using namespace doc::event_list;
using doc_util::event_builder::at;
using std::move;

/// Contains all fields of PatternRef except the event list.
struct PatternMetadata {
    BlockIndex idx;

    /// Absolute timestamps.
    TickT t0;
    TickT t1;

    /// True if this is the first loop.
    bool first = false;
    /// True if this is the last loop.
    bool last = false;

    size_t nev;
};

static void verify_all(
    SequenceTrack const& track, std::vector<PatternMetadata> expected_patterns
) {
    auto iter = TrackPatternIter::at_time(track, 0).iter;

    for (auto [i_, expected] : enumerate<size_t>(expected_patterns)) {
        auto i = i_; CAPTURE(i);

        auto next = iter.peek(track);
        iter.next(track);

        REQUIRE_UNARY(next);
        CHECK(next->block == expected.idx);
        CHECK(next->begin_tick == expected.t0);
        CHECK(next->end_tick == expected.t1);
        CHECK(next->is_block_begin == expected.first);
        CHECK(next->is_block_end == expected.last);
        CHECK(next->events.size() == expected.nev);
    }

    auto next = iter.peek(track);
    CHECK_UNARY_FALSE(next);
}

static SequenceTrack single_block(int num_events) {
    EventList events;
    assert(num_events > 0);
    for (int i = 0; i < num_events; i++) {
        events.push_back({i, {(NoteInt) i}});
    }

    return SequenceTrack{TrackBlock::from_events(0, num_events, move(events))};
}

TEST_CASE("Check TrackPatternIter with a single block") {
    verify_all(single_block(4), {PatternMetadata{
        .idx = 0, .t0 = 0, .t1 = 4, .first = true, .last = true, .nev = 4
    }});
}

static SequenceTrack single_block_loop(int num_events, uint32_t loop_count) {
    EventList events;
    assert(num_events > 0);
    for (int i = 0; i < num_events; i++) {
        events.push_back({i, {(NoteInt) i}});
    }

    return SequenceTrack{TrackBlock::from_events(
        0, num_events, move(events), loop_count
    )};
}

// Looped block, END_OF_GRID.
TEST_CASE("Check TrackPatternIter with a looped block") {
    verify_all(single_block_loop(1, 4), {
        PatternMetadata{.idx = 0, .t0 = 0, .t1 = 1, .first = true, .nev = 1},
        PatternMetadata{.idx = 0, .t0 = 1, .t1 = 2, .nev = 1},
        PatternMetadata{.idx = 0, .t0 = 2, .t1 = 3, .nev = 1},
        PatternMetadata{.idx = 0, .t0 = 3, .t1 = 4, .last = true, .nev = 1},
    });
}

// TODO Check TrackPatternIter with a looped block truncated by its ending

/// The two_blocks(_loop1) test cases are less configurable than the single_block(_loop)
/// and need to be reworked to be more configurable.
static SequenceTrack two_blocks() {
    return SequenceTrack{
        TrackBlock::from_events(0, 4, {
            {0, {0}}
        }),
        TrackBlock::from_events(6, 2, {
            {0, {1}},
        }),
    };
}

TEST_CASE("Check TrackPatternIter with multiple in-bounds blocks") {
    verify_all(two_blocks(), {
        PatternMetadata{
            .idx = 0, .t0 = 0, .t1 = 4, .first = true, .last = true, .nev = 1
        },
        PatternMetadata{
            .idx = 1, .t0 = 6, .t1 = 8, .first = true, .last = true, .nev = 1
        },
    });
}

#if 0
static SequenceTrack two_blocks_loop1() {
    return SequenceTrack{
        TrackBlock::from_events(0, 4, {
            {0, {0}}
        }, 4),
        TrackBlock::from_events(6, 2, {
            {0, {1}},
        }, 2),
    };
}

/// Zero-length patterns are currently not constructible through the UI,
/// Though they would be convenient in a MML setting.
static SequenceTrack has_zero_length_block() {
    return SequenceTrack{
        TrackBlock{0, 4,
            Pattern{EventList{
                {0, {0}},
            }}
        },
        TrackBlock{4, 4,
            Pattern{EventList{
                {0, {NOTE_CUT}},
            }}
        },
    };
}

TEST_CASE("Check zero-length blocks before the end of the track") {
    verify_all(has_zero_length_block(), 5, {
        PatternMetadata{
            .idx = 0, .t0 = 0, .t1 = 4, .first = true, .last = true, .nev = 1
        },
        // Currently, zero-length blocks have their contents cleared.
        // This is because only events with time < end are kept,
        // which excludes events at time start (= end).
        // TODO TrackPatternIter should special-case zero-length blocks
        // and allow events taking place at the end (change this test to .nev = 1).
        PatternMetadata{
            .idx = 1, .t0 = 4, .t1 = 4, .first = true, .last = true, .nev = 0
        },
    });
}

TEST_CASE("Check zero-length blocks at the end of the track") {
    verify_all(has_zero_length_block(), 4, {
        PatternMetadata{
            .idx = 0, .t0 = 0, .t1 = 4, .first = true, .last = true, .nev = 1
        },
        // TODO It would be useful to keep zero-length blocks at the end of a grid track,
        // to reset effects and such.
        // However, I cannot allow row 0 of a nonzero-length block or loop to play
        // at the end of a grid track.
        // Keeping zero-length blocks would be an inconsistency.
        // Currently they are skipped.
    });
}

TEST_CASE("Check zero-length blocks before the end of the track") {
    verify_all(has_zero_length_block(), 3, {
        PatternMetadata{
            .idx = 0, .t0 = 0, .t1 = 3, .first = true, .last = true, .nev = 1
        },
    });
}
#endif

TEST_SUITE_END();
} // namespaces

#endif
