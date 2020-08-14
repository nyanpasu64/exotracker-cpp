#include "timeline.h"
#include "doc_util/event_search.h"

namespace doc::timeline {

TimelineCellIter::TimelineCellIter(GridCell nbeats)
    : _cell_nbeats(nbeats.nbeats)
{}

using event_list::EventIndex;
using doc_util::event_search::EventSearch;

EventIndex calc_end_ev(TimedEventsRef events, BeatFraction rel_end_time) {
    EventSearch kv{events};
    return EventIndex(kv.beat_begin(rel_end_time) - events.begin());
}

/// TODO this needs a unit test *so badly*
MaybePatternRef TimelineCellIter::next(TimelineCell const& cell) {
    scrBegin;

    for (_block = 0; _block < cell.size(); _block++) {
        // hopefully the optimizer will cache these macros' values

        #define BLOCK  (cell._raw_blocks[_block])  // type: TimelineBlock

        #define BLOCK_END_TIME  (BLOCK.end_time.value_or(_cell_nbeats))  // type: BeatFraction
        #define LOOP_LENGTH  (int(BLOCK.pattern.loop_length))  // type: MaybeNonZero<uint32_t>

        if (LOOP_LENGTH) {
            _loop_ev_idx = calc_end_ev(BLOCK.pattern.events, BLOCK.pattern.loop_length);

            // Blocks where END_TIME <= begin_time are invalid, but skip them anyway.
            for (
                _loop_begin_time = BLOCK.begin_time;
                _loop_begin_time < BLOCK_END_TIME;
                _loop_begin_time += LOOP_LENGTH
            ) {
                scrBeginScope;

                auto loop_end_time = std::min(
                    BeatFraction(_loop_begin_time + LOOP_LENGTH), BLOCK_END_TIME
                );

                bool is_block_begin = _loop_begin_time == BLOCK.begin_time;
                bool is_block_end = loop_end_time == BLOCK_END_TIME;

                // The final loop may or may not be truncated.
                // Unconditionally recompute the end event for simplicity.
                EventIndex end_ev_idx = is_block_end
                    ? calc_end_ev(BLOCK.pattern.events, BLOCK_END_TIME - _loop_begin_time)
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
                calc_end_ev(BLOCK.pattern.events, BLOCK_END_TIME - BLOCK.begin_time);
            scrReturnEndScope((PatternRef{
                .block = _block,
                .begin_time = BLOCK.begin_time,
                .end_time = BLOCK_END_TIME,
                .is_block_begin = true,
                .is_block_end = true,
                .events = TimedEventsRef(BLOCK.pattern.events).subspan(0, end_ev_idx)
            }));
        }
    }

    while (true) {
        scrReturn(std::nullopt);
    }

    scrFinishUnreachable;

    throw std::logic_error("Reached end of TimelineCellIter");
}

TimelineCellIterRef::TimelineCellIterRef(TimelineCell const& cell, GridCell nbeats)
    : _cell(cell)
    , _iter(nbeats)
{}

MaybePatternRef TimelineCellIterRef::next() {
    return _iter.next(_cell);
}

}
