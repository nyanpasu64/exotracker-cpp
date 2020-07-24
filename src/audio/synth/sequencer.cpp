#define ChannelSequencer_INTERNAL public
#include "sequencer.h"
#include "util/release_assert.h"

#include <fmt/core.h>

#include <algorithm>  // std::min
#include <limits>  // std::numeric_limits
#include <type_traits>  // std::is_signed_v

namespace audio::synth::sequencer {

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

using TimedEventsRef = gsl::span<doc::TimedRowEvent const>;

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
}

doc::MaybeSeqEntryIndex calc_next_entry(
    doc::Document const & document, doc::SeqEntryIndex seq_entry_index
) {
    // exotracker will have no pattern-jump effects.
    // Instead, each "order entry" has a length field, and a "what to do next" field.

    // If seq entry jumps to another seq index, return that one instead.

    seq_entry_index++;
    if (seq_entry_index >= document.sequence.size()) {
        // If seq entry can halt song afterwards, return {}.
        return 0;
    }
    return {seq_entry_index};

    // If seq entry can jump partway into a pattern,
    // change function to return both a pattern and a beat.
}

static BeatPlusTick frac_to_tick(TickT ticks_per_beat, BeatFraction beat) {
    doc::FractionInt ibeat = beat.numerator() / beat.denominator();
    BeatFraction fbeat = beat - ibeat;

    doc::FractionInt dtick = doc::round_to_int(fbeat * ticks_per_beat);

    ibeat += dtick / ticks_per_beat;
    dtick %= ticks_per_beat;

    return BeatPlusTick{.beat=ibeat, .dtick=dtick};
}

static void advance_event_seq_entry(
    EventIterator & next_event, doc::Document const & document
) {
    next_event.event_idx = 0;
    next_event.prev_seq_entry = next_event.seq_entry;

    auto next_seq_entry = calc_next_entry(document, next_event.seq_entry);
    if (next_seq_entry.has_value()) {
        next_event.seq_entry = *next_seq_entry;
    } else {
        // TODO halt playback
        next_event.seq_entry = 0;
    }
};

static void check_invariants(ChannelSequencer const & self) {
    // If two sequential patterns are different,
    // assert that they don't take up the same region in time.
    // But if they're the same, they could be different occurrences of a loop.
    if (self._next_event.seq_entry != self._now.seq_entry) {
        release_assert(self._pattern_offset.event_minus_now() != 0);
    }
};

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
};

std::tuple<SequencerTime, EventsRef> ChannelSequencer::next_tick(
    doc::Document const & document
) {
    _events_this_tick.clear();

    // Assert that seek() was called earlier.
    release_assert(_curr_ticks_per_beat != 0);

    // Document-level operations, not bound to current sequence entry.
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
        (uint16_t) _now.seq_entry,
        (uint16_t) ticks_per_beat,
        (int16_t) _now.next_tick.beat,
        (int16_t) _now.next_tick.dtick,
    };

    BeatPlusTick const now_pattern_len = [&] {
        doc::SequenceEntry const & now_entry = document.sequence[_now.seq_entry];
        return frac_to_tick(ticks_per_beat, now_entry.nbeats);
    }();

    TimedEventsRef events;
    BeatPlusTick ev_pattern_len;

    auto get_events = [
        this, &document, nchip, nchan, ticks_per_beat,
        // mutate these
        &events, &ev_pattern_len
    ]() {
        doc::SequenceEntry const & current_entry =
            document.sequence[_next_event.seq_entry];
        auto & chip_channel_events = current_entry.chip_channel_events;

        // [chip_index]
        release_assert(chip_channel_events.size() == nchip);
        auto & channel_events = chip_channel_events[_chip_index];

        // [chip_index][chan_index]
        release_assert(channel_events.size() == nchan);
        events = channel_events[_chan_index];

        ev_pattern_len = frac_to_tick(ticks_per_beat, current_entry.nbeats);
    };

    check_invariants(*this);
    get_events();

    // We want to look for the next event in the song, to check whether to play it.
    while (true) {
        while (_next_event.event_idx >= events.size()) {
            // Current pattern has no more events to play. Check the next pattern.

            if (!_pattern_offset.advance_event()) {
                // _next_event is already 1 pattern past real time.
                // No unvisited events in previous, current, or next pattern. Quit.
                goto end_loop;
            }

            advance_event_seq_entry(_next_event, document);
            check_invariants(*this);
            get_events();
        }

        doc::TimedRowEvent next_ev = events[_next_event.event_idx];

        // Quantize event to (beat integer, tick offset), to match now.
        // Note that *.beat is int, not BeatFraction!
        BeatPlusTick now = _now.next_tick;
        BeatPlusTick next_ev_time =
            frac_to_tick(ticks_per_beat, next_ev.time.anchor_beat);
        next_ev_time.dtick += next_ev.time.tick_offset;

        // Only scanning for events in the next pattern
        // reduces worst-case CPU usage in the 6502 driver.
        // Only supporting overhang in the last beat
        // reduces the risk of integer overflow.

        if (_pattern_offset.event_is_ahead()) {
            // If event is on next pattern,
            // and _now is over 1 beat from reaching the next pattern,
            // don't play the next pattern.
            if (now.beat + 1 < now_pattern_len.beat) {
                goto end_loop;
            }

            // Treat next pattern as starting at time 0.
            now -= now_pattern_len;

        } else if (_pattern_offset.event_is_behind()) {
            // If event is on previous pattern, wait indefinitely.
            // Treat previous pattern as ending at time 0.
            next_ev_time -= ev_pattern_len;

        } else {
            // Event is on current pattern.
            // If event is anchored over 1 beat ahead of now, don't play it.
            if (now.beat + 1 < next_ev_time.beat) {
                goto end_loop;
            }
        }

        // Compare quantized times, check if event has occurred or not.
        auto event_pos = event_vs_now(ticks_per_beat, now, next_ev_time);

        // Past events are overdue and should never happen.
        if (event_pos == EventPos::Past) {
            auto time = next_ev.time;
            fmt::print(
                stderr,
                "invalid document: event at seq {} time {}/{} + {} is in the past!\n",
                _next_event.seq_entry,
                time.anchor_beat.numerator(),
                time.anchor_beat.denominator(),
                time.tick_offset
            );
        }

        // Past and present events should be played.
        if (event_pos != EventPos::Future) {
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
    // You can't assert that now_tick.beat <= pattern_len.beat.
    // That could happen on a speed=1 zero-length pattern (pathological but not worth crashing on).
    // Nor can you assert that if now_tick.beat == pattern_len.beat, now_tick.dtick <= pattern_len.dtick.
    // That could happen on a speed>1 zero-length pattern.

    // If we reach the end of the pattern, advance to the next.
    // Even if the next pattern has zero length, don't advance again.
    if (now_tick >= now_pattern_len) {
        // If advancing now would leave events from 2 patterns ago in the queue,
        // we need to drop them.
        if (!_pattern_offset.advance_now()) {
            fmt::print(
                stderr,
                "invalid document: event at sequence entry {} extends past 2 patterns!\n",
                _next_event.seq_entry
            );

            // These commented-out operations are not strictly necessary.
            // release_assert(_pattern_offset.advance_event());
            advance_event_seq_entry(_next_event, document);
            // release_assert(_pattern_offset.advance_time());
        }
        now_tick = {0, 0};

        auto next_seq_entry = calc_next_entry(document, _now.seq_entry);
        if (next_seq_entry.has_value()) {
            _now.seq_entry = *next_seq_entry;
        } else {
            // TODO halt playback
            _now.seq_entry = 0;
        }

        check_invariants(*this);
    }

    return {seq_chan_time, _events_this_tick};
}

void ChannelSequencer::seek(doc::Document const & document, PatternAndBeat time) {
    // Document-level operations, not bound to current sequence entry.
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

        _now = RealTime{.seq_entry=time.seq_entry_index, .next_tick=now_ticks};
    }

    // Set next event to play.
    {
        _pattern_offset = PatternOffset{};

        _next_event.prev_seq_entry = {};
        _next_event.seq_entry = time.seq_entry_index;
        _next_event.event_idx = 0;
    }

    // Advance _next_event to the correct event_idx.

    // Note that seeking is done in beat-fraction space (ignoring offsets),
    // so replace _now.next_tick with time.beat,
    // and frac_to_tick(...next_ev.time.anchor_beat). with next_ev.time.anchor_beat.

    TimedEventsRef events;

    auto get_events = [
        this, &document, nchip, nchan,
        // mutate these
        &events
    ]() {
        doc::SequenceEntry const & current_entry =
            document.sequence[_next_event.seq_entry];
        auto & chip_channel_events = current_entry.chip_channel_events;

        // [chip_index]
        release_assert(chip_channel_events.size() == nchip);
        auto & channel_events = chip_channel_events[_chip_index];

        // [chip_index][chan_index]
        release_assert(channel_events.size() == nchan);
        events = channel_events[_chan_index];
    };

    check_invariants(*this);
    get_events();

    // Skip events in past, queue first now/future event (only looking at anchor beat).
    while (true) {
        while (_next_event.event_idx >= events.size()) {
            // Current pattern has no more events to play. Check the next pattern.

            if (!_pattern_offset.advance_event()) {
                // _next_event is already 1 pattern past real time.
                // No unvisited events in previous, current, or next pattern. Quit.
                goto end_loop;
            }

            advance_event_seq_entry(_next_event, document);
            check_invariants(*this);
            get_events();
        }

        doc::TimedRowEvent next_ev = events[_next_event.event_idx];

        auto now = time.beat;
        auto next_ev_time = next_ev.time.anchor_beat;

        if (_pattern_offset.event_is_ahead()) {
            // If event is on next pattern, queue it for playback.
            goto end_loop;

        } else {
            // Since we just reset _pattern_offset and are advancing in event time,
            // real time cannot be ahead of events,
            // and _pattern_offset.event_is_behind() is impossible.
            assert(!_pattern_offset.event_is_behind());

            // Event is on current pattern.
            // If event is now/future, queue it for playback.
            if (next_ev_time >= now) {
                goto end_loop;
            }
        }

        // _next_event.event_idx may be out of bounds.
        // The next iteration of the outer loop will fix this.
        _next_event.event_idx++;
    }
    end_loop:
    return;
}

/*
We need separate APIs for "pattern contents changed" and "document speed changed".
Right now, we recompute event index based on _now
(which is correct if pattern contents change).
If document speed changes, we should instead recompute _now and not event index.

To recompute _now, we can convert _now to a beat fraction (= dtick / ticks per beat),
then round down when converting back to a tick
(to avoid putting events in the past as much as possible).
*/

void ChannelSequencer::doc_edited(doc::Document const & document) {
    // Document-level operations, not bound to current sequence entry.
    auto const nchip = document.chips.size();
    release_assert(_chip_index < nchip);
    auto const nchan = document.chip_index_to_nchan(_chip_index);
    release_assert(_chan_index < nchan);

    doc::SequencerOptions const options = document.sequencer_options;
    TickT const ticks_per_beat = options.ticks_per_beat;

    // *everything* needs to be overhauled when I add mid-song tempo changes.
    release_assert_equal(_curr_ticks_per_beat, ticks_per_beat);

    // Set next event to play.
    if (
        _next_event.prev_seq_entry.has_value() && !_pattern_offset.event_is_behind()
    ) {
        _next_event.seq_entry = *_next_event.prev_seq_entry;
        _next_event.prev_seq_entry = {};
        _pattern_offset.advance_now();
    }

    _next_event.event_idx = 0;

    // Advance _next_event to the correct event_idx, in real time.
    BeatPlusTick const now_pattern_len = [&] {
        doc::SequenceEntry const & now_entry = document.sequence[_now.seq_entry];
        return frac_to_tick(ticks_per_beat, now_entry.nbeats);
    }();

    TimedEventsRef events;
    BeatPlusTick ev_pattern_len;

    auto get_events = [
        this, &document, nchip, nchan, ticks_per_beat,
        // mutate these
        &events, &ev_pattern_len
    ]() {
        doc::SequenceEntry const & current_entry =
            document.sequence[_next_event.seq_entry];
        auto & chip_channel_events = current_entry.chip_channel_events;

        // [chip_index]
        release_assert(chip_channel_events.size() == nchip);
        auto & channel_events = chip_channel_events[_chip_index];

        // [chip_index][chan_index]
        release_assert(channel_events.size() == nchan);
        events = channel_events[_chan_index];

        ev_pattern_len = frac_to_tick(ticks_per_beat, current_entry.nbeats);
    };

    check_invariants(*this);
    get_events();

    // Skip events in past, queue first now/future event (in real time).
    while (true) {
        while (_next_event.event_idx >= events.size()) {
            // Current pattern has no more events to play. Check the next pattern.

            if (!_pattern_offset.advance_event()) {
                // _next_event is already 1 pattern past real time.
                // No unvisited events in previous, current, or next pattern. Quit.
                goto end_loop;
            }

            advance_event_seq_entry(_next_event, document);
            check_invariants(*this);
            get_events();
        }

        doc::TimedRowEvent next_ev = events[_next_event.event_idx];

        // Quantize event to (beat integer, tick offset), to match now.
        // Note that *.beat is int, not BeatFraction!
        BeatPlusTick now = _now.next_tick;
        BeatPlusTick next_ev_time =
            frac_to_tick(ticks_per_beat, next_ev.time.anchor_beat);
        next_ev_time.dtick += next_ev.time.tick_offset;

        // On the first loop iteration, we move _next_event backwards in time
        // so _pattern_offset.event_is_ahead() cannot happen.
        // On subsequent iterations, it can happen.
        // All 3 cases are necessary.

        if (_pattern_offset.event_is_ahead()) {
            // If event is on next pattern,
            // and _now is over 1 beat from reaching the next pattern,
            // don't play the next pattern.
            if (now.beat + 1 < now_pattern_len.beat) {
                goto end_loop;
            }

            // Treat next pattern as starting at time 0.
            now -= now_pattern_len;

        } else if (_pattern_offset.event_is_behind()) {
            // If event is on previous pattern, wait indefinitely.
            // Treat previous pattern as ending at time 0.
            next_ev_time -= ev_pattern_len;

        } else {
            // Event is on current pattern.
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
    return;
}

void ChannelSequencer::tempo_changed(doc::Document const & document) {
    // beat must be based on the current value of _now,
    // not the previously returned "start of beat".
    // Or else, reassigning _now could erase pattern transitions
    // and break invariants.

    // Assert that seek() was called earlier.
    release_assert(_curr_ticks_per_beat != 0);

    doc::BeatFraction beat{
        _now.next_tick.beat + doc::BeatFraction{
            _now.next_tick.dtick, _curr_ticks_per_beat
        }
    };
    TickT const ticks_per_beat = document.sequencer_options.ticks_per_beat;

    // Set real time.
    _now.next_tick = frac_to_tick(ticks_per_beat, beat);
}

// end namespaces
}
