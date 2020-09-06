#include "timeline.h"
#include "doc_util/event_search.h"
#include "util/release_assert.h"

#include <algorithm>  // std::min

namespace doc::timeline {

using event_list::EventIndex;
using doc_util::event_search::EventSearch;

EventIndex calc_end_ev(TimedEventsRef events, BeatFraction rel_end_time) {
    EventSearch kv{events};
    return EventIndex(kv.beat_begin(rel_end_time) - events.begin());
}

MaybePatternRef TimelineCellIter::next(TimelineCellRef cell_ref) {
    scrBegin;

    for (_block = 0; _block < cell_ref.cell.size(); _block++) {
        // hopefully the optimizer will cache these macros' values

        #define BLOCK  (cell_ref.cell._raw_blocks[_block])  // type: TimelineBlock

        // The cell may have invalid block layouts, if the user shrinks a timeline cell,
        // or if we load malformed external documents.

        // If a block starts past the cell, discard it.
        if (BLOCK.begin_time >= cell_ref.nbeats) {
            goto end_loop;
        }

        // If a block ends past the cell, truncate it.
        _block_end_time =
            std::min(BLOCK.end_time.value_or(cell_ref.nbeats), cell_ref.nbeats);

        /*
        Blocks should not end before they begin (have a negative length).
        The GUI will not allow directly resizing a block
        such that `_block_end_time < BLOCK.begin_time`.
        However this can occur if a block ends at the cell end
        and the cell's length is decreased.
        This *should* be caught by the BLOCK.begin_time check.

        However, malformed external files may have an end time < begin time.
        This should be caught at file-load time.
        But add an explicit check just to make sure.
        */
        release_assert(_block_end_time >= BLOCK.begin_time);

        #define LOOP_LENGTH  (MaybeNonZero<int>(BLOCK.pattern.loop_length))
        if (LOOP_LENGTH) {
            _loop_ev_idx = calc_end_ev(BLOCK.pattern.events, BLOCK.pattern.loop_length);

            for (
                _loop_begin_time = BLOCK.begin_time;
                _loop_begin_time < _block_end_time;
                _loop_begin_time += LOOP_LENGTH
            ) {
                scrBeginScope;

                auto loop_end_time = std::min(
                    BeatFraction(_loop_begin_time + LOOP_LENGTH), _block_end_time
                );

                bool is_block_begin = _loop_begin_time == BLOCK.begin_time;
                bool is_block_end = loop_end_time == _block_end_time;

                // The final loop may or may not be truncated.
                // Unconditionally recompute the end event for simplicity.
                EventIndex end_ev_idx = is_block_end
                    ? calc_end_ev(BLOCK.pattern.events, _block_end_time - _loop_begin_time)
                    : _loop_ev_idx;

                scrReturnEndScope((PatternRef{
                    .block = _block,
                    .begin_time = _loop_begin_time,
                    .end_time = loop_end_time,
                    .is_block_begin = is_block_begin,
                    .is_block_end = is_block_end,
                    .events = TimedEventsRef(BLOCK.pattern.events).subspan(0, end_ev_idx)
                }));
            }
        } else {
            scrBeginScope;
            EventIndex end_ev_idx =
                calc_end_ev(BLOCK.pattern.events, _block_end_time - BLOCK.begin_time);
            scrReturnEndScope((PatternRef{
                .block = _block,
                .begin_time = BLOCK.begin_time,
                .end_time = _block_end_time,
                .is_block_begin = true,
                .is_block_end = true,
                .events = TimedEventsRef(BLOCK.pattern.events).subspan(0, end_ev_idx)
            }));
        }
    }

    end_loop:
    while (true) {
        scrReturn(std::nullopt);
    }

    scrFinishUnreachable;

    throw std::logic_error("Reached end of TimelineCellIter");
}

TimelineCellIterRef::TimelineCellIterRef(TimelineCellRef cell_ref)
    : _cell_ref(cell_ref)
{}

MaybePatternRef TimelineCellIterRef::next() {
    return _iter.next(_cell_ref);
}

}

#ifdef UNITTEST

#include "doc_util/shorthand.h"
#include "util/compare.h"
#include "util/compare_impl.h"
#include "util/enumerate.h"

#include <doctest.h>

namespace doc::timeline {
TEST_SUITE_BEGIN("doc/timeline");

using doc_util::shorthand::at;
using std::move;

/// Contains all fields of PatternRef except the event list.
struct PatternMetadata {
    BlockIndex idx;

    /// Timestamps within the current grid cell.
    BeatIndex t0;
    BeatFraction t1;

    /// True if this is the first loop.
    bool first = false;
    /// True if this is the last loop.
    bool last = false;

    size_t nev;
};

static void verify_all(
    TimelineCell const& cell,
    BeatFraction nbeats,
    std::vector<PatternMetadata> expected_patterns
) {
    TimelineCellRef cell_ref{nbeats, cell};
    TimelineCellIter iter;

    for (auto [i_, expected] : enumerate<size_t>(expected_patterns)) {
        auto i = i_; CAPTURE(i);

        auto next = iter.next(cell_ref);
        REQUIRE_UNARY(next);
        CHECK(next->block == expected.idx);
        CHECK(next->begin_time == expected.t0);
        CHECK(next->end_time == expected.t1);
        CHECK(next->is_block_begin == expected.first);
        CHECK(next->is_block_end == expected.last);
        CHECK(next->events.size() == expected.nev);
    }

    for (int i = 0; i < 2; i++) {
        CAPTURE(i);
        auto next = iter.next(cell_ref);
        CHECK_UNARY_FALSE(next);
    }
}

static TimelineCell single_block(BeatOrEnd end_time) {
    EventList events;
    auto num_events = (int) end_time.value_or(1u);
    for (int i = 0; i < num_events; i++) {
        events.push_back({at(i), {i}});
    }

    return TimelineCell{TimelineBlock{0, end_time,
        Pattern{move(events)}
    }};
}

TEST_CASE("Check TimelineCellIter with a single block filling the entire grid cell") {
    verify_all(single_block(END_OF_GRID), 4, {PatternMetadata{
        .idx = 0, .t0 = 0, .t1 = 4, .first = true, .last = true, .nev = 1
    }});
}

TEST_CASE("Check TimelineCellIter with a single block ending before the grid cell") {
    verify_all(single_block(4), 5, {PatternMetadata{
        .idx = 0, .t0 = 0, .t1 = 4, .first = true, .last = true, .nev = 4
    }});
}

TEST_CASE("Check TimelineCellIter with a single block overflowing the grid cell") {
    verify_all(single_block(4), 3, {PatternMetadata{
        .idx = 0, .t0 = 0, .t1 = 3, .first = true, .last = true, .nev = 3
    }});
}

static TimelineCell single_block_loop(BeatOrEnd end_time, uint32_t loop_modulo) {
    EventList events;
    for (uint32_t i = 0; i < loop_modulo; i++) {
        events.push_back({at(i), {i}});
    }

    return TimelineCell{TimelineBlock{0, end_time,
        Pattern{move(events), loop_modulo}
    }};
}

// Looped block, END_OF_GRID.
TEST_CASE("Check TimelineCellIter with a looped block filling the entire grid cell") {
    verify_all(single_block_loop(END_OF_GRID, 1), 4, {
        PatternMetadata{.idx = 0, .t0 = 0, .t1 = 1, .first = true, .nev = 1},
        PatternMetadata{.idx = 0, .t0 = 1, .t1 = 2, .nev = 1},
        PatternMetadata{.idx = 0, .t0 = 2, .t1 = 3, .nev = 1},
        PatternMetadata{.idx = 0, .t0 = 3, .t1 = 4, .last = true, .nev = 1},
    });
}

TEST_CASE(
    "Check TimelineCellIter with a full-grid looped block truncated by the grid cell"
) {
    verify_all(single_block_loop(END_OF_GRID, 3), 4, {
        PatternMetadata{.idx = 0, .t0 = 0, .t1 = 3, .first = true, .nev = 3},
        PatternMetadata{.idx = 0, .t0 = 3, .t1 = 4, .last = true, .nev = 1},
    });
}

// Looped block, fixed length.
TEST_CASE("Check TimelineCellIter with a looped block ending before the grid cell") {
    verify_all(single_block_loop(4, 1), 5, {
        PatternMetadata{.idx = 0, .t0 = 0, .t1 = 1, .first = true, .nev = 1},
        PatternMetadata{.idx = 0, .t0 = 1, .t1 = 2, .nev = 1},
        PatternMetadata{.idx = 0, .t0 = 2, .t1 = 3, .nev = 1},
        PatternMetadata{.idx = 0, .t0 = 3, .t1 = 4, .last = true, .nev = 1},
    });
}

TEST_CASE("Check TimelineCellIter with a looped block ending after the grid cell") {
    verify_all(single_block_loop(4, 1), 3, {
        PatternMetadata{.idx = 0, .t0 = 0, .t1 = 1, .first = true, .nev = 1},
        PatternMetadata{.idx = 0, .t0 = 1, .t1 = 2, .nev = 1},
        PatternMetadata{.idx = 0, .t0 = 2, .t1 = 3, .last = true, .nev = 1},
    });
}

TEST_CASE("Check TimelineCellIter with a looped block truncated by its ending") {
    verify_all(single_block_loop(4, 3), 100, {
        PatternMetadata{.idx = 0, .t0 = 0, .t1 = 3, .first = true, .nev = 3},
        PatternMetadata{.idx = 0, .t0 = 3, .t1 = 4, .last = true, .nev = 1},
    });
}

TEST_CASE("Check TimelineCellIter with a looped block truncated by the grid cell") {
    verify_all(single_block_loop(5, 3), 4, {
        PatternMetadata{.idx = 0, .t0 = 0, .t1 = 3, .first = true, .nev = 3},
        PatternMetadata{.idx = 0, .t0 = 3, .t1 = 4, .last = true, .nev = 1},
    });
}

/// The two_blocks(_loop1) test cases are less configurable than the single_block(_loop)
/// and need to be reworked to be more configurable.
static TimelineCell two_blocks() {
    return TimelineCell{
        TimelineBlock{0, 4,
            Pattern{EventList{
                {at(0), {0}},
            }}
        },
        TimelineBlock{6, 8,
            Pattern{EventList{
                {at(0), {1}},
            }}
        },
    };
}

TEST_CASE("Check TimelineCellIter with multiple in-bounds blocks") {
    verify_all(two_blocks(), 10, {
        PatternMetadata{
            .idx = 0, .t0 = 0, .t1 = 4, .first = true, .last = true, .nev = 1
        },
        PatternMetadata{
            .idx = 1, .t0 = 6, .t1 = 8, .first = true, .last = true, .nev = 1
        },
    });
}

TEST_CASE("Check TimelineCellIter with multiple out-of-bounds blocks") {
    verify_all(two_blocks(), 1, {
        PatternMetadata{
            .idx = 0, .t0 = 0, .t1 = 1, .first = true, .last = true, .nev = 1
        },
        // The second TimelineBlock should not be yielded at all.
    });
}

static TimelineCell two_blocks_loop1() {
    return TimelineCell{
        TimelineBlock{0, 4,
            Pattern{EventList{
                {at(0), {0}},
            }, 1}
        },
        TimelineBlock{6, 8,
            Pattern{EventList{
                {at(0), {1}},
            }, 1}
        },
    };
}

TEST_CASE("Check TimelineCellIter with out-of-bounds looped blocks") {
    verify_all(two_blocks_loop1(), 3, {
        PatternMetadata{.idx = 0, .t0 = 0, .t1 = 1, .first = true, .nev = 1},
        PatternMetadata{.idx = 0, .t0 = 1, .t1 = 2, .nev = 1},
        PatternMetadata{.idx = 0, .t0 = 2, .t1 = 3, .last = true, .nev = 1},
        // the rest should be skipped.
    });
}

/// Zero-length patterns are currently not constructible through the UI,
/// Though they would be convenient in a MML setting.
static TimelineCell has_zero_length_block() {
    return TimelineCell{
        TimelineBlock{0, 4,
            Pattern{EventList{
                {at(0), {0}},
            }}
        },
        TimelineBlock{4, 4,
            Pattern{EventList{
                {at(0), {NOTE_CUT}},
            }}
        },
    };
}

TEST_CASE("Check zero-length blocks before the end of the cell") {
    verify_all(has_zero_length_block(), 5, {
        PatternMetadata{
            .idx = 0, .t0 = 0, .t1 = 4, .first = true, .last = true, .nev = 1
        },
        // Currently, zero-length blocks have their contents cleared.
        // This is because only events with time < end are kept,
        // which excludes events at time start (= end).
        // TODO TimelineCellIter should special-case zero-length blocks
        // and allow events taking place at the end (change this test to .nev = 1).
        PatternMetadata{
            .idx = 1, .t0 = 4, .t1 = 4, .first = true, .last = true, .nev = 0
        },
    });
}

TEST_CASE("Check zero-length blocks at the end of the cell") {
    verify_all(has_zero_length_block(), 4, {
        PatternMetadata{
            .idx = 0, .t0 = 0, .t1 = 4, .first = true, .last = true, .nev = 1
        },
        // TODO It would be useful to keep zero-length blocks at the end of a grid cell,
        // to reset effects and such.
        // However, I cannot allow row 0 of a nonzero-length block or loop to play
        // at the end of a grid cell.
        // Keeping zero-length blocks would be an inconsistency.
        // Currently they are skipped.
    });
}

TEST_CASE("Check zero-length blocks before the end of the cell") {
    verify_all(has_zero_length_block(), 3, {
        PatternMetadata{
            .idx = 0, .t0 = 0, .t1 = 3, .first = true, .last = true, .nev = 1
        },
    });
}

TEST_SUITE_END();
}

#endif
