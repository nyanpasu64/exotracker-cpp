#define ChannelSequencer_INTERNAL public
#include "sequencer.h"
#include "util/compare_impl.h"
#include "util/format.h"
#include "util/release_assert.h"
#include "util/math.h"

#include <fmt/core.h>

#include <algorithm>  // std::min
#include <limits>  // std::numeric_limits
#include <string>
#include <type_traits>  // std::is_signed_v

//#define SEQUENCER_DEBUG

namespace audio::synth::sequencer {

COMPARABLE_IMPL(BeatPlusTick, (self.beat, self.dtick))

using doc::BeatFraction;

// TODO add support for grooves.
// We need to remove usages of `doc::round_to_int()`
// once we allow changing the speed mid-song.

// Unused for now.
[[maybe_unused]]
static TickT time_to_ticks(doc::TimeInPattern time, doc::SequencerOptions options) {
    return doc::round_to_int(time.anchor_beat * options.ticks_per_beat)
        + time.tick_offset;
}

using doc::TimedEventsRef;

enum class EventPos {
    Past,
    Now,
    Future,
};

ChannelSequencer::ChannelSequencer() {
    /*
    On ticks without events, ChannelSequencer should return a 0-length vector.
    On ticks with events, ChannelSequencer should return a 1-length vector.

    The only time we should return more than 1 event is with broken documents,
    where multiple events occur at the same time
    (usually due to early events being offset later,
    or later events being offset earlier).

    Later events prevent earlier events from being offset later;
    instead they will pile up at the same time as the later event.

    We should never reach or exceed 4 events simultaneously.
    */
    _events_this_tick.reserve(4);
    stop_playback();  // initializes _curr_ticks_per_beat
}

void ChannelSequencer::stop_playback() {
    _now = {};

    // Set is-playing to false.
    _curr_ticks_per_beat = 0;

    _ignore_ordering_errors = false;
    _next_event = {};
    _grid_runahead = {};
}


// # Grid-cell/pattern code.

doc::MaybeGridIndex calc_next_grid(
    doc::Timeline const& timeline, doc::GridIndex grid_index
) {
    grid_index++;
    if ((size_t) grid_index >= timeline.size()) {
        // If song ends, return {}.
        return doc::GridIndex(0);
    }
    return {grid_index};

    // If seq entry can jump partway into a grid cell,
    // change function to return both a gridline and a beat.
}

struct NextPattern {
    /// Whether we reached the end of the song and looped to the begin.
    /// If true, the sequencer can choose to stop.
    bool wrapped;

    doc::GridIndex grid;
    TimelineCellIter pattern_iter_after;

    /// If next grid cell is empty, returns no pattern.
    doc::MaybePatternRef pattern;
};

/// Preconditions:
/// - Timeline must be non-empty (all valid documents have at least 1 grid cell).
///
/// Advances to the next pattern (all blocks are flattened) and returns it.
/// If no more patterns in current grid cell, returns first pattern in next grid cell.
/// If next grid cell is empty, gives up and returns no pattern.
///
/// Returns whether we wrapped around the end of the song,
/// next pattern, and iterator pointing after next pattern.
///
/// The caller of this function is expected to handle this case.
static NextPattern calc_next_pattern(
    doc::TimelineChannelRef timeline,
    doc::GridIndex grid,
    TimelineCellIter pattern_iter
) {
    bool wrapped = false;

    auto pattern = pattern_iter.next(timeline[grid]);
    if (!pattern) {
        grid++;
        if (grid >= timeline.size()) {
            grid = 0;
            wrapped = true;
        }

        pattern_iter = TimelineCellIter();
        pattern = pattern_iter.next(timeline[grid]);
    }

    return NextPattern{wrapped, grid, pattern_iter, pattern};
}

struct EventIteratorResult {
    EventIterator next_event{};
    bool switched_grid{};
};

/// Preconditions:
/// - Timeline must be non-empty (all valid documents have at least 1 grid cell).
///
/// Return = did we switch grid cells.
[[nodiscard]] static EventIteratorResult ev_iter_advance_pattern(
    doc::TimelineChannelRef timeline,
    EventIterator orig_event
) {
    NextPattern v = calc_next_pattern(
        timeline, orig_event.grid, *orig_event.pattern_iter
    );
    bool switched_cells = v.wrapped || v.grid != orig_event.grid;

    // TODO if v.wrapped and document looping disabled, halt playback.
    EventIterator new_event{
        .prev_grid = switched_cells ? orig_event.grid : orig_event.prev_grid,
        .grid = v.grid,
        .pattern_iter = v.pattern_iter_after,
        .pattern = v.pattern
            ? std::optional(PatternIndex::from(*v.pattern))
            : std::nullopt,
        .event_idx = 0,
    };
    return EventIteratorResult{new_event, switched_cells};
}


// # ChannelSequencer::next_tick() helpers.

static void check_invariants(ChannelSequencer const & self) {
    // _next_event and _now must point to the same or adjacent patterns.
    // If the patterns belong to different grid cells,
    // assert that the sequencer knows _next_event and _now are desynced.
    // But if they're the same grid cell, they don't have to be synced
    // (a one-cell document could loop).
    if (self._next_event.grid != self._now.grid) {
        release_assert(self._grid_runahead.event_minus_now() != 0);
    }
}

EventPos event_vs_now(TickT ticks_per_beat, BeatPlusTick now, BeatPlusTick ev) {
    TickT ev_minus_now =
        ticks_per_beat * (ev.beat - now.beat) + (ev.dtick - now.dtick);
    if (ev_minus_now > 0) {
        return EventPos::Future;
    } else if (ev_minus_now < 0) {
        return EventPos::Past;
    } else {
        return EventPos::Now;
    }
}

void print_chip_channel(ChannelSequencer const& self) {
    fmt::print(stderr, "seq {},{} ", self._chip_index, self._chan_index);
}


using RoundFrac = doc::FractionInt (*)(BeatFraction);

template<RoundFrac round_frac = doc::round_to_int>
static BeatPlusTick frac_to_tick(TickT ticks_per_beat, BeatFraction beat) {
    doc::FractionInt ibeat = beat.numerator() / beat.denominator();
    BeatFraction fbeat = beat - ibeat;

    doc::FractionInt dtick = round_frac(fbeat * ticks_per_beat);

    ibeat += dtick / ticks_per_beat;
    dtick %= ticks_per_beat;

    return BeatPlusTick{.beat=ibeat, .dtick=dtick};
}


std::tuple<SequencerTime, EventsRef> ChannelSequencer::next_tick(
    doc::Document const & document
) {
    _events_this_tick.clear();

    // Assert that seek() was called earlier.
    release_assert(_curr_ticks_per_beat != 0);
    release_assert(_next_event.pattern_iter.has_value());

    #ifdef SEQUENCER_DEBUG
    print_chip_channel(*this);
    fmt::print(stderr,
        "begin tick, grid {}, beat time {}+{}\n",
        _now.grid,
        _now.next_tick.beat,
        _now.next_tick.dtick
    );
    fmt::print(stderr,
        "\tcurrent event grid {} pattern at {}, index {}\n",
        _next_event.grid,
        _next_event.pattern->begin_time,
        _next_event.event_idx
    );
    #endif

    // Document-level operations, not bound to current channel/grid/pattern.
    auto const nchip = document.chips.size();
    release_assert(_chip_index < nchip);
    auto const nchan = document.chip_index_to_nchan(_chip_index);
    release_assert(_chan_index < nchan);

    doc::SequencerOptions const options = document.sequencer_options;
    TickT const ticks_per_beat = options.ticks_per_beat;
    _curr_ticks_per_beat = ticks_per_beat;

    // SequencerTime is current tick (just occurred), not next tick.
    // This is a subjective choice?
    SequencerTime const seq_chan_time {
        (uint16_t) _now.grid,
        (uint16_t) ticks_per_beat,
        (int16_t) _now.next_tick.beat,
        (int16_t) _now.next_tick.dtick,
    };

    auto timeline =
        doc::TimelineChannelRef(document.timeline, _chip_index, _chan_index);

    BeatPlusTick const now_grid_len =
        frac_to_tick(ticks_per_beat, timeline[_now.grid].nbeats);

    TimedEventsRef events{};
    doc::BeatIndex pattern_start{};
    BeatPlusTick ev_grid_len{};

    auto get_events = [
        this, &timeline, ticks_per_beat,
        // mutate these
        &events, &pattern_start, &ev_grid_len
    ]() {
        doc::TimelineCellRef cell_ref = timeline[_next_event.grid];

        // mutate environment
        if (_next_event.pattern) {
            PatternIndex pattern = *_next_event.pattern;
            events = TimedEventsRef(
                cell_ref.cell._raw_blocks[pattern.block].pattern.events
            ).subspan(0, pattern.num_events);
            pattern_start = _next_event.pattern->begin_time;

        } else {
            events = {};
            pattern_start = 0;
        }

        ev_grid_len = frac_to_tick(ticks_per_beat, cell_ref.nbeats);
    };

    check_invariants(*this);
    get_events();

    // We want to look for the next event in the song, to check whether to play it.
    while (true) {
        while (_next_event.event_idx >= events.size()) {
            // Current pattern has no more events to play. Check the next pattern.

            auto result = ev_iter_advance_pattern(timeline, _next_event);
            if (result.switched_grid) {
                if (!_grid_runahead.advance_event_grid()) {
                    // _next_event is already 1 cell past real time.
                    // No unvisited events in previous, current, or next cell. Quit.
                    goto end_loop;
                }
            }
            _next_event = result.next_event;

            check_invariants(*this);
            // Mutates events, pattern_start, ev_grid_len
            get_events();
        }

        doc::TimedRowEvent next_ev = events[_next_event.event_idx];

        // Quantize event to (beat integer, tick offset), to match now.
        // Note that *.beat is int, not BeatFraction!
        BeatPlusTick now = _now.next_tick;

        BeatPlusTick next_ev_time =
            frac_to_tick(ticks_per_beat, next_ev.time.anchor_beat);
        next_ev_time.beat += pattern_start;
        next_ev_time.dtick += next_ev.time.tick_offset;

        // Only scanning for events in the next grid cell(?)
        // reduces worst-case CPU usage in the 6502 driver.
        // Only supporting overhang in the last beat
        // reduces the risk of integer overflow.

        if (_grid_runahead.event_grid_ahead()) {
            #ifdef SEQUENCER_DEBUG
            fmt::print(stderr, "\tevent is ahead now\n");
            #endif

            // If next event's block is on next grid cell,
            // and _now is over 1 beat from reaching the next grid cell,
            // don't play the event.
            if (now.beat + 1 < now_grid_len.beat) {
                goto end_loop;
            }

            // Treat next grid cell as starting at time 0.
            now -= now_grid_len;

        } else if (_grid_runahead.event_grid_behind()) {
            #ifdef SEQUENCER_DEBUG
            fmt::print(stderr, "\tevent is behind now\n");
            #endif

            // If event is on previous grid cell, wait indefinitely.
            // Treat previous grid cell as ending at time 0.
            next_ev_time -= ev_grid_len;

        } else {
            // Event is on current grid cell.
            // If event is anchored over 1 beat ahead of now, don't play it.
            if (now.beat + 1 < next_ev_time.beat) {
                goto end_loop;
            }
        }

        // Compare quantized times, check if event has occurred or not.
        auto event_pos = event_vs_now(ticks_per_beat, now, next_ev_time);

        // Past events are overdue and should never happen.
        if (!_ignore_ordering_errors && event_pos == EventPos::Past) {
            auto time = next_ev.time;
            fmt::print(
                stderr,
                "invalid document: event at grid {} pattern at {} time {} + {} is in the past!\n",
                _next_event.grid,
                _next_event.pattern->begin_time,
                format_frac(time.anchor_beat),
                time.tick_offset
            );
        }

        // Past and present events should be played.
        if (event_pos != EventPos::Future) {
            #ifdef SEQUENCER_DEBUG
            fmt::print(
                stderr,
                "\tplaying event beat {} -> time {}+{}\n",
                format_frac(next_ev.time.anchor_beat),
                next_ev_time.beat,
                next_ev_time.dtick
            );
            #endif

            _events_this_tick.push_back(next_ev.v);

            // _next_event.event_idx may be out of bounds.
            // The next iteration of the outer loop will fix this.
            _next_event.event_idx++;
            continue;
        }

        // Future events can wait.
        goto end_loop;
        // This reminds me of EventQueue.
        // But that doesn't support inserting overdue events in the past
        // (due to tracker user error).
    }
    end_loop:

    auto & now_tick = _now.next_tick;
    now_tick.dtick++;
    // if tempo changes suddenly,
    // it's possible now_tick.dtick could exceed ticks_per_beat.
    // I'm not sure if it happens. let's not assert for now.
    // How about asserting that now_tick.dtick / ticks_per_beat
    // can never exceed 1? Not sure.

    auto dbeat = now_tick.dtick / ticks_per_beat;
    release_assert(0 <= dbeat);
    release_assert(dbeat <= 1);

    now_tick.beat += now_tick.dtick / ticks_per_beat;
    now_tick.dtick %= ticks_per_beat;
    // You can't assert that now_tick.beat <= now_grid_len.beat.
    // That could happen on a speed=1 zero-length grid cell (pathological but not worth crashing on).
    // Nor can you assert that if now_tick.beat == now_grid_len.beat, now_tick.dtick <= now_grid_len.dtick.
    // That could happen on a speed>1 zero-length grid cell.

    // If `now` reaches the end of the grid cell, advance to the next.
    // Even if the next grid cell has zero length, don't advance again.
    if (now_tick >= now_grid_len) {
        // If advancing now would leave events from 2 cells ago in the queue,
        // we need to drop them.
        if (!_grid_runahead.advance_now_grid()) {
            fmt::print(
                stderr,
                "invalid document: event at grid {} pattern at {} delayed past 2 gridlines!\n",
                _next_event.grid,
                _next_event.pattern->begin_time
            );

            while (true) {
                auto result =
                    ev_iter_advance_pattern(timeline, _next_event);
                if (result.switched_grid) {
                    _next_event = result.next_event;
                    break;
                }
            }
        }
        now_tick = {0, 0};

        auto next_grid = calc_next_grid(document.timeline, _now.grid);
        if (next_grid.has_value()) {
            _now.grid = *next_grid;
        } else {
            // TODO halt playback
            _now.grid = doc::GridIndex(0);
        }

        check_invariants(*this);
    }

    _ignore_ordering_errors = false;
    return {seq_chan_time, _events_this_tick};
}


void ChannelSequencer::seek(doc::Document const & document, GridAndBeat time) {
    #ifdef SEQUENCER_DEBUG
    print_chip_channel(*this);
    fmt::print(stderr, "seek {}, {}\n", time.grid, format_frac(time.beat));
    #endif

    // Document-level operations, not bound to current channel/grid/pattern.
    auto const nchip = document.chips.size();
    release_assert(_chip_index < nchip);
    auto const nchan = document.chip_index_to_nchan(_chip_index);
    release_assert(_chan_index < nchan);

    doc::SequencerOptions const options = document.sequencer_options;
    TickT const ticks_per_beat = options.ticks_per_beat;

    // Set is-playing to true.
    _curr_ticks_per_beat = ticks_per_beat;

    // Set real time.
    {
        BeatPlusTick now_ticks = frac_to_tick(ticks_per_beat, time.beat);

        _now = RealTime{.grid=time.grid, .next_tick=now_ticks};
    }

    auto timeline =
        doc::TimelineChannelRef(document.timeline, _chip_index, _chan_index);
    auto nbeats = document.timeline[time.grid];

    // Set next event to play.
    _grid_runahead = GridRunahead{};

    _next_event = EventIterator{
        .prev_grid = {},
        .grid = time.grid,
        .pattern_iter = TimelineCellIter(),
        .pattern = {},
        .event_idx = 0,
    };

    // Advance _next_event to the correct event_idx.

    // Note that seeking is done in beat-fraction space (ignoring offsets),
    // so replace _now.next_tick with time.beat,
    // and frac_to_tick(...next_ev.time.anchor_beat). with next_ev.time.anchor_beat.

    TimedEventsRef events{};
    doc::BeatIndex pattern_start{};

    auto get_events = [
        this, &timeline,
        // mutate these
        &events, &pattern_start
    ]() {
        doc::TimelineCellRef cell_ref = timeline[_next_event.grid];

        // mutate environment
        if (_next_event.pattern) {
            PatternIndex pattern = *_next_event.pattern;
            events = TimedEventsRef(
                cell_ref.cell._raw_blocks[pattern.block].pattern.events
            ).subspan(0, pattern.num_events);
            pattern_start = _next_event.pattern->begin_time;

        } else {
            events = {};
            pattern_start = 0;
        }
    };

    check_invariants(*this);
    get_events();

    // Skip events in past, queue first now/future event (only looking at anchor beat).
    while (true) {
        while (_next_event.event_idx >= events.size()) {
            // Current pattern has no more events to play. Check the next pattern.

            auto result = ev_iter_advance_pattern(timeline, _next_event);
            if (result.switched_grid) {
                if (!_grid_runahead.advance_event_grid()) {
                    // _next_event is already 1 cell past real time.
                    // No unvisited events in previous, current, or next cell. Quit.
                    goto end_loop;
                }
            }
            _next_event = result.next_event;

            check_invariants(*this);
            // Mutates events, pattern_start, ev_grid_len
            get_events();
        }

        if (_grid_runahead.event_grid_ahead()) {
            // If event is on next grid cell, queue it for playback.
            goto end_loop;

        } else {
            // Since we just reset _grid_runahead and are advancing in event time,
            // real time cannot be ahead of events,
            // and _grid_runahead.event_grid_behind() is impossible.
            release_assert(!_grid_runahead.event_grid_behind());

            doc::TimedRowEvent next_ev = events[_next_event.event_idx];

            BeatFraction now = time.beat;
            BeatFraction next_ev_time = pattern_start + next_ev.time.anchor_beat;

            // Event is on current grid cell.
            // If event is now/future, queue it for playback.
            if (next_ev_time >= now) {
                goto end_loop;
            }

            // _next_event.event_idx may be out of bounds.
            // The next iteration of the outer loop will fix this.
            _next_event.event_idx++;
        }
    }
    end_loop:
    // Users may use early notes at the beginning or middle of patterns.
    // If the user initiates playback at a time with early notes,
    // just play them immediately instead of complaining about past notes,
    // since the document isn't invalid.
    _ignore_ordering_errors = true;
}

/*
We provide separate APIs for "pattern contents changed" and "document speed changed".
doc_edited() recomputes event index based on _now
(which is correct if pattern contents change).
tempo_changed() recomputes _now and not event index.

To recompute _now, we can convert _now to a beat fraction (= dtick / ticks per beat),
then round down when converting back to a tick
(to avoid putting events in the past as much as possible).
*/

void ChannelSequencer::tempo_changed(doc::Document const & document) {
    #ifdef SEQUENCER_DEBUG
    print_chip_channel(*this);
    fmt::print(stderr, "tempo_changed {}\n", document.sequencer_options.ticks_per_beat);
    #endif

    // beat must be based on the current value of _now,
    // not the previously returned "start of beat".
    // Or else, reassigning _now could erase gridline crossings
    // and break _grid_runahead invariants.

    // Assert that seek() was called earlier.
    release_assert(_curr_ticks_per_beat != 0);

    doc::BeatFraction beat{
        _now.next_tick.beat + doc::BeatFraction{
            _now.next_tick.dtick, _curr_ticks_per_beat
        }
    };
    TickT const ticks_per_beat = document.sequencer_options.ticks_per_beat;

    // Set real time.
    // Both numerator (_now.next_tick) and denominator (_curr_ticks_per_beat)
    // need to be changed at the same time.
    _now.next_tick = frac_to_tick<util::math::frac_floor>(ticks_per_beat, beat);
    _curr_ticks_per_beat = ticks_per_beat;

    /*
    Sometimes it's impossible to avoid putting events in the past.
    Suppose you start with _now = 0 and ticks/beat = 1.

    When next_tick() is called at ticks/beat = 1:
    - _next_event (ceil-rounded) will advance until it's greater than _now
      (1/4 rounds up to 1 > _now=0)
    - _now will advance by 1 tick (_now := 1)

    When tempo_changed() is called at ticks/beat = 11:
    - _now stays at 1

    When next_tick() is called at ticks/beat = 11:
    - _next_event is at 1/4, which rounds to 3.
    - _now rounds to 11.
    - The event is in the past.

    How would we fix this?
    Setting frac_to_tick to frac_floor() (towards the past)
    seems to resolve time paradoxes in the absence of delayed/early notes.
    Switching between custom user grooves can cause semi-random time paradoxes.
    Delayed/early notes can cause unavoidable time paradoxes
    (not just due to rounding/quantization).

    So set a flag saying "ignore past event errors".
    */
    _ignore_ordering_errors = true;
}

void ChannelSequencer::doc_edited(doc::Document const & document) {
    #ifdef SEQUENCER_DEBUG
    print_chip_channel(*this);
    fmt::print(stderr, "doc_edited\n");
    #endif

    // Document-level operations, not bound to current channel/grid/pattern.
    auto const nchip = document.chips.size();
    release_assert(_chip_index < nchip);
    auto const nchan = document.chip_index_to_nchan(_chip_index);
    release_assert(_chan_index < nchan);

    doc::SequencerOptions const options = document.sequencer_options;
    TickT const ticks_per_beat = options.ticks_per_beat;

    // *everything* needs to be overhauled when I add mid-song tempo changes.
    release_assert_equal(_curr_ticks_per_beat, ticks_per_beat);

    // Set next event to play.
    /*
    TODO add tests for:
    - switching blocks in the middle of a grid
    - doc_edited(), prev_grid unset
    - doc_edited(), prev_grid set
    - doc_edited(), current event delayed past loop border
    - doc_edited(), current event delayed past block border
    - doc_edited(), current event delayed past grid border
    - doc_edited(), current event advanced before loop border
    - doc_edited(), current event advanced before block border
    - doc_edited(), current event advanced before grid border
    */
    if (
        _next_event.prev_grid && !_grid_runahead.event_grid_behind()
    ) {
        doc::GridIndex grid = *_next_event.prev_grid;
        auto nbeats = document.timeline[grid];

        bool success = _grid_runahead.advance_now_grid();
        release_assert(success);

        _next_event = EventIterator{
            .prev_grid = {},
            .grid = grid,
            .pattern_iter = TimelineCellIter(),
            .pattern = {},
            .event_idx = 0,
        };

    } else {
        auto prev_grid = _next_event.prev_grid;
        auto grid = _next_event.grid;
        auto nbeats = document.timeline[grid];

        _next_event = EventIterator{
            .prev_grid = prev_grid,
            .grid = grid,
            .pattern_iter = TimelineCellIter(),
            .pattern = {},
            .event_idx = 0,
        };
    }

    auto timeline =
        doc::TimelineChannelRef(document.timeline, _chip_index, _chan_index);

    BeatPlusTick const now_grid_len =
        frac_to_tick(ticks_per_beat, timeline[_now.grid].nbeats);

    TimedEventsRef events{};
    doc::BeatIndex pattern_start{};
    BeatPlusTick ev_grid_len{};

    auto get_events = [
        this, &timeline, ticks_per_beat,
        // mutate these
        &events, &pattern_start, &ev_grid_len
    ]() {
        doc::TimelineCellRef cell_ref = timeline[_next_event.grid];

        // mutate environment
        if (_next_event.pattern) {
            PatternIndex pattern = *_next_event.pattern;
            events = TimedEventsRef(
                cell_ref.cell._raw_blocks[pattern.block].pattern.events
            ).subspan(0, pattern.num_events);
            pattern_start = _next_event.pattern->begin_time;

        } else {
            events = {};
            pattern_start = 0;
        }

        ev_grid_len = frac_to_tick(ticks_per_beat, cell_ref.nbeats);
    };

    check_invariants(*this);
    get_events();

    // Skip events in past, queue first now/future event (in real time).
    while (true) {
        while (_next_event.event_idx >= events.size()) {
            // Current pattern has no more events to play. Check the next pattern.

            auto result = ev_iter_advance_pattern(timeline, _next_event);
            if (result.switched_grid) {
                if (!_grid_runahead.advance_event_grid()) {
                    // _next_event is already 1 cell past real time.
                    // No unvisited events in previous, current, or next cell. Quit.
                    goto end_loop;
                }
            }
            _next_event = result.next_event;

            check_invariants(*this);
            // Mutates events, pattern_start, ev_grid_len
            get_events();
        }

        doc::TimedRowEvent next_ev = events[_next_event.event_idx];

        // Quantize event to (beat integer, tick offset), to match now.
        // Note that *.beat is int, not BeatFraction!
        BeatPlusTick now = _now.next_tick;

        BeatPlusTick next_ev_time =
            frac_to_tick(ticks_per_beat, next_ev.time.anchor_beat);
        next_ev_time.beat += pattern_start;
        next_ev_time.dtick += next_ev.time.tick_offset;

        // On the first loop iteration, we move _next_event backwards in time
        // (prev_grid above) so _grid_runahead.event_grid_ahead() cannot happen.
        // On subsequent iterations, it can happen.
        // All 3 cases are necessary.

        if (_grid_runahead.event_grid_ahead()) {
            #ifdef SEQUENCER_DEBUG
            fmt::print(stderr, "\tevent is ahead now\n");
            #endif

            // If next event's pattern is on next grid cell,
            // and _now is over 1 beat from reaching the next grid cell,
            // don't play the event.
            if (now.beat + 1 < now_grid_len.beat) {
                goto end_loop;
            }

            // Treat next grid cell as starting at time 0.
            now -= now_grid_len;

        } else if (_grid_runahead.event_grid_behind()) {
            #ifdef SEQUENCER_DEBUG
            fmt::print(stderr, "\tevent is behind now\n");
            #endif

            // If event is on previous grid cell, wait indefinitely.
            // Treat previous grid cell as ending at time 0.
            next_ev_time -= ev_grid_len;

        } else {
            // Event is on current grid cell.
            // If event is anchored over 1 beat ahead of now, don't play it.
            if (now.beat + 1 < next_ev_time.beat) {
                goto end_loop;
            }
        }

        // Compare quantized times, check if event has occurred or not.
        auto event_pos = event_vs_now(ticks_per_beat, now, next_ev_time);
        if (event_pos == EventPos::Past) {
            // Skip past events.
            // _next_event.event_idx may be out of bounds.
            // The next iteration of the outer loop will fix this.
            _next_event.event_idx++;
        } else {
            // Queue present/future events for playback.
            goto end_loop;
        }
    }
    end_loop:
    _ignore_ordering_errors = false;
}

void ChannelSequencer::timeline_modified(doc::Document const & document) {
    // Clamp the grid cell within the document.
    // This MUST be the first operation in this function!
    // TODO supply an API so deleting previous grids moves the cursor backwards,
    // and adding previous grids (or undoing deletion) moves the cursor forwards.
    _now.grid = std::min(_now.grid, doc::GridIndex(document.timeline.size() - 1));

    // Reset the next event to play, to the in-bounds grid cell.
    _grid_runahead = {};
    _next_event = {.grid = _now.grid};

    // Clamp the cursor within the in-bounds grid cell's length.
    BeatPlusTick const now_grid_len = ({
        doc::SequencerOptions const options = document.sequencer_options;
        TickT const ticks_per_beat = options.ticks_per_beat;
        auto timeline =
            doc::TimelineChannelRef(document.timeline, _chip_index, _chan_index);
        frac_to_tick(ticks_per_beat, timeline[_now.grid].nbeats);
    });

    /*
    doc_edited() treats adjacent grid cells as a continuum.
    If _now.next_tick is at or past the end of one grid cell,
    it acts as if _now is in the next cell (due to `now -= now_grid_len`)
    and skips playing events within the overhang.

    To fix this issue, clamp the tick to the current grid
    (including the endpoint, because it's easier than subtracting 1 row or tick
    or jumping to the next pattern).

    doc_edited() will advance to the next grid's tick 0
    and play the first event that isn't early.
    */
    _now.next_tick = std::min(_now.next_tick, now_grid_len);

    // Recompute the next event to play.
    doc_edited(document);
}

// end namespaces
}
