#include "sequencer.h"

#include "util/release_assert.h"

#ifdef UNITTEST
#include <doctest.h>
#endif

#include <algorithm>  // std::min
#include <limits>  // std::numeric_limits
#include <type_traits>  // std::is_signed_v

namespace audio::synth::sequencer {

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

EventsRef ChannelSequencer::next_tick(
    doc::Document const & document, ChipIndex chip_index, ChannelIndex chan_index
) {
    _events_this_tick.clear();

    // Document-level operations, not bound to current sequence entry.
    auto const nchip = document.chips.size();
    release_assert(chip_index < nchip);
    auto const nchan = document.chip_index_to_nchan(chip_index);
    release_assert(chan_index < nchan);

    doc::SequencerOptions const options = document.sequencer_options;
    TickT const ticks_per_beat = options.ticks_per_beat;

    auto frac_to_tick = [ticks_per_beat](doc::BeatFraction beat) -> BeatPlusTick {
        doc::FractionInt ibeat = beat.numerator() / beat.denominator();
        doc::BeatFraction fbeat = beat - ibeat;

        doc::FractionInt dtick = doc::round_to_int(fbeat * ticks_per_beat);
        return BeatPlusTick{.beat=ibeat, .dtick=dtick};
    };

    auto distance = [ticks_per_beat](BeatPlusTick from, BeatPlusTick to) -> TickT {
        return ticks_per_beat * (to.beat - from.beat) + to.dtick - from.dtick;
    };

    BeatPlusTick const now_pattern_len = [&] {
        doc::SequenceEntry const & now_entry = document.sequence[_now.seq_entry];
        return frac_to_tick(now_entry.nbeats);
    }();

    auto advance_event_seq_entry = [this, &document]() {
        _next_event.event_idx = 0;
        _next_event.prev_seq_entry = _next_event.seq_entry;

        auto next_seq_entry = calc_next_entry(document, _next_event.seq_entry);
        if (next_seq_entry.has_value()) {
            _next_event.seq_entry = *next_seq_entry;
        } else {
            // TODO halt playback
            _next_event.seq_entry = 0;
        }
    };

    auto check_invariants = [this]() {
        // If two sequential patterns are different,
        // assert that they don't take up the same region in time.
        // But if they're the same, they could be different occurrences of a loop.
        if (_next_event.seq_entry != _now.seq_entry) {
            release_assert(_pattern_offset.event_minus_now() != 0);
        }
    };

    TimedEventsRef events;
    BeatPlusTick ev_pattern_len;

    auto get_events = [
        this, &document, chip_index, chan_index, nchip, nchan, &frac_to_tick,
        // mutate these
        &events, &ev_pattern_len
    ]() {
        doc::SequenceEntry const & current_entry =
            document.sequence[_next_event.seq_entry];
        auto & chip_channel_events = current_entry.chip_channel_events;

        // [chip_index]
        release_assert(chip_channel_events.size() == nchip);
        auto & channel_events = chip_channel_events[chip_index];

        // [chip_index][chan_index]
        release_assert(channel_events.size() == nchan);
        events = channel_events[chan_index];

        ev_pattern_len = frac_to_tick(current_entry.nbeats);
    };

    check_invariants();
    get_events();

    while (true) {
        while (_next_event.event_idx >= events.size()) {
            // Current pattern is empty.

            if (!_pattern_offset.advance_event()) {
                // _next_event is already 1 pattern past real time.
                // No unvisited events in previous, current, or next pattern. Quit.
                goto end_loop;
            }

            advance_event_seq_entry();
            check_invariants();
            get_events();
        }

        doc::TimedRowEvent next_ev = events[_next_event.event_idx];

        BeatPlusTick now = _now.next_tick;
        BeatPlusTick next_ev_time = frac_to_tick(next_ev.time.anchor_beat);
        next_ev_time.dtick += next_ev.time.tick_offset;

        // Only scanning for events in the next pattern
        // reduces worst-case CPU usage in the 6502 driver.
        // Only supporting overhang in the last beat
        // reduces the risk of integer overflow.

        if (_pattern_offset.event_is_ahead()) {
            // Don't look too far ahead.
            // Ensure _now is within 1 beat of finishing the pattern.
            // Otherwise don't bother playing any notes.
            if (!(now.beat + 1 >= now_pattern_len.beat)) {
                break;
            }

            now -= now_pattern_len;

        } else if (_pattern_offset.event_is_behind()) {
            next_ev_time -= ev_pattern_len;

        } else {
            // Ensure _now is within 1 beat of the next event.
            // Otherwise don't bother playing any notes.
            if (!(now.beat + 1 >= next_ev_time.beat)) {
                break;
            }
        }

        TickT now_to_event = distance(now, next_ev_time);
        if (now_to_event < 0) {
            fmt::print(
                "invalid document: event at seq {} time {} is in the past!\n",
                _next_event.seq_entry,
                next_ev.time.anchor_beat
            );
        }
        if (now_to_event <= 0) {
            _events_this_tick.push_back(next_ev.v);

            // _next_event.event_idx may be out of bounds.
            // The next iteration of the outer loop will fix this.
            _next_event.event_idx++;
            continue;
        }

        break;
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
                "invalid document: event at sequence entry {} extends past 2 patterns!\n",
                _next_event.seq_entry
            );

            // These commented-out operations are not strictly necessary.
            // release_assert(_pattern_offset.advance_event());
            advance_event_seq_entry();
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

        check_invariants();
    }

    return _events_this_tick;
}

#ifdef UNITTEST
// I could use DOCTEST_CONFIG_DISABLE to disable tests outside of the testing target,
// but I don't know how to #undef only in exotracker-tests
// via target_compile_definitions.

// I found some interesting advice for building comprehensive code tests:
// "Rethinking Software Testing: Perspectives from the world of Hardware"
// https://software.rajivprab.com/2019/04/28/rethinking-software-testing-perspectives-from-the-world-of-hardware/

// (tests removed)

#endif

// end namespaces
}
