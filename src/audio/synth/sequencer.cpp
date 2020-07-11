#define ChannelSequencer_INTERNAL public
#include "sequencer.h"

#include "util/release_assert.h"

#include <algorithm>  // std::min
#include <limits>  // std::numeric_limits
#include <type_traits>  // std::is_signed_v

#ifdef UNITTEST
    #include "chip_kinds.h"
    #include "sample_docs.h"
    #include "edit_util/shorthand.h"
    #include "test_utils/parameterize.h"

    #include <fmt/core.h>
    #include <doctest.h>
    #include <gsl/span_ext>  // span == span

    #include <random>

    namespace timing {
        std::ostream& operator<< (std::ostream& os, SequencerTime const & value) {
            os << fmt::format("SequencerTime{{{}, {}, {}, {}}}",
                value.seq_entry_index,
                value.curr_ticks_per_beat,
                value.beats,
                value.ticks
            );
            return os;
        }
    }
#endif

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

    // Document-level operations, not bound to current sequence entry.
    auto const nchip = document.chips.size();
    release_assert(_chip_index < nchip);
    auto const nchan = document.chip_index_to_nchan(_chip_index);
    release_assert(_chan_index < nchan);

    doc::SequencerOptions const options = document.sequencer_options;
    TickT const ticks_per_beat = options.ticks_per_beat;

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

#ifdef UNITTEST
// I could use DOCTEST_CONFIG_DISABLE to disable tests outside of the testing target,
// but I don't know how to #undef only in exotracker-tests
// via target_compile_definitions.

// I found some interesting advice for building comprehensive code tests:
// "Rethinking Software Testing: Perspectives from the world of Hardware"
// https://software.rajivprab.com/2019/04/28/rethinking-software-testing-perspectives-from-the-world-of-hardware/

using namespace doc;
using chip_kinds::ChipKind;
using namespace edit_util::shorthand;

static Document simple_doc() {
    SequencerOptions sequencer_options{
        .ticks_per_beat = 10,
    };

    Sequence sequence;

    // seq ind 0
    sequence.push_back([] {
        BeatFraction nbeats = 2;

        ChipChannelTo<EventList> chip_channel_events;
        chip_channel_events.push_back({
            // channel 0
            {
                // TimeInPattern, RowEvent
                {at(0), {0}},
                {at(1), {1}},
            },
            // channel 1
            {},
        });

        return SequenceEntry {
            .nbeats = nbeats,
            .chip_channel_events = chip_channel_events,
        };
    }());

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .instruments = Instruments(),
        .chips = {ChipKind::Apu1},
        .sequence = sequence
    };
}

static ChannelSequencer make_channel_sequencer(
    ChipIndex chip_index, ChannelIndex chan_index
) {
    ChannelSequencer seq;
    seq.set_chip_chan(chip_index, chan_index);
    return seq;
}

/// To test the seuqencer's "document edit" handling functionality,
/// optionally send a "document edited" signal before every single tick
/// and make sure the output is unchanged.
PARAMETERIZE(should_reload_doc, bool, should_reload_doc,
    OPTION(should_reload_doc, false);
    OPTION(should_reload_doc, true);
)

TEST_CASE("Test basic sequencer") {
    auto document = simple_doc();
    auto seq = make_channel_sequencer(0, 0);

    bool reload_doc;
    PICK(should_reload_doc(reload_doc));
    auto next_tick = [&] () {
        if (reload_doc) {
            seq.doc_edited(document);
        }
        return seq.next_tick(document);
    };

    for (int pat = 0; pat < 2; pat++) {
        CAPTURE(pat);

        // Beat 0
        {
            auto [t, ev] = next_tick();
            CHECK(t == SequencerTime{0, t.curr_ticks_per_beat, 0, 0});
            CHECK(ev.size() == 1);
            if (ev.size() == 1) {
                CHECK(ev[0].note == 0);
            }
        }

        for (int16_t i = 1; i < 10; i++) {
            auto [t, ev] = next_tick();
            CHECK(t == SequencerTime{0, t.curr_ticks_per_beat, 0, i});
            CHECK(ev.size() == 0);
        }

        // Beat 1
        {
            auto [t, ev] = next_tick();
            CHECK(t == SequencerTime{0, t.curr_ticks_per_beat, 1, 0});
            CHECK(ev.size() == 1);
            if (ev.size() == 1) {
                CHECK(ev[0].note == 1);
            }
        }

        for (int16_t i = 1; i < 10; i++) {
            auto [t, ev] = next_tick();
            CHECK(t == SequencerTime{0, t.curr_ticks_per_beat, 1, i});
            CHECK(ev.size() == 0);
        }
    }
}

/// Too simple IMO.
TEST_CASE("Test seeking") {
    auto document = simple_doc();
    auto seq = make_channel_sequencer(0, 0);

    bool reload_doc;
    PICK(should_reload_doc(reload_doc));
    auto next_tick = [&] () {
        if (reload_doc) {
            seq.doc_edited(document);
        }
        return seq.next_tick(document);
    };

    seq.seek(document, PatternAndBeat{0, {1, 2}});

    for (int16_t i = 5; i < 10; i++) {
        auto [t, ev] = next_tick();
        CHECK(t == SequencerTime{0, t.curr_ticks_per_beat, 0, i});
        CHECK(ev.size() == 0);
    }

    // Beat 1
    {
        auto [t, ev] = next_tick();
        CHECK(t == SequencerTime{0, t.curr_ticks_per_beat, 1, 0});
        CHECK(ev.size() == 1);
        if (ev.size() == 1) {
            CHECK(ev[0].note == 1);
        }
    }
}

TEST_CASE("Ensure sequencer behaves the same with and without reloading position") {
    char const * doc_names[] {"dream-fragments", "world-revolution"};
    for (auto doc_name : doc_names) {
        CAPTURE(doc_name);
        Document const & document = sample_docs::DOCUMENTS.at(doc_name);

        for (
            ChannelIndex chan = 0;
            chan < CHIP_TO_NCHAN[(size_t) ChipKind::Apu1];
            chan++
        ) {
            CAPTURE(chan);

            auto normal = make_channel_sequencer(0, chan);
            auto reload = make_channel_sequencer(0, chan);

            int normal_loops = 0;
            int reload_loops = 0;
            while (true) {
                auto [normal_time, normal_ev] = normal.next_tick(document);
                if (
                    normal_time
                    == SequencerTime{0, normal_time.curr_ticks_per_beat, 0, 0}
                ) {
                    normal_loops++;
                }

                reload.doc_edited(document);
                auto [reload_time, reload_ev] = reload.next_tick(document);
                if (
                    reload_time
                    == SequencerTime{0, reload_time.curr_ticks_per_beat, 0, 0}
                ) {
                    reload_loops++;
                }

                CHECK(normal_time == reload_time);
                CHECK(normal_ev == reload_ev);

                if (normal_loops == 2 || reload_loops == 2) {
                    break;
                }
            }
        }
    }
}

/// Simple document which can be parameterized to test document editing.
///
/// beat = 0 or 1.
/// delay = [0, 10) or so.
static Document parametric_doc(uint32_t beat, TickT delay) {
    SequencerOptions sequencer_options{
        .ticks_per_beat = 10,
    };

    Sequence sequence;

    // seq ind 0
    sequence.push_back([&] {
        BeatFraction nbeats = 4;

        ChipChannelTo<EventList> chip_channel_events;
        chip_channel_events.push_back({
            // channel 0
            {
                // TimeInPattern, RowEvent
                {at_delay(beat, delay), {0}},
                {at_delay(beat + 2, -delay), {1}},
            },
            // channel 1
            {},
        });

        return SequenceEntry {
            .nbeats = nbeats,
            .chip_channel_events = chip_channel_events,
        };
    }());

    // The second pattern is ✨different✨.
    delay = 10 - delay;

    // seq ind 1
    sequence.push_back([&] {
        BeatFraction nbeats = 4;

        ChipChannelTo<EventList> chip_channel_events;
        chip_channel_events.push_back({
            // channel 0
            {
                // TimeInPattern, RowEvent
                {at_delay(beat, delay), {2}},
                {at_delay(beat + 2, -delay), {3}},
            },
            // channel 1
            {},
        });

        return SequenceEntry {
            .nbeats = nbeats,
            .chip_channel_events = chip_channel_events,
        };
    }());

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .instruments = Instruments(),
        .chips = {ChipKind::Apu1},
        .sequence = sequence
    };
}

std::random_device rd;

TEST_CASE("Randomly switch between randomly generated documents") {
    // Keep two sequencers and tick both in lockstep.
    // Occasionally switch documents.
    // Upon each mutation, one sequencer is reset and advanced from scratch,
    // and the other is informed the document has changed.

    // okay i wasted how many hours on this test that caught absolutely nothing
    // and is completely unnecessary given the way I implemented doc_edited()...
    // (doc_edited() ignores our position in the old document, and only keeps _now.)

    using RNG = std::minstd_rand;
    RNG rng{rd()};

    for (int i = 0; i < 100; i++) {
        // Capture state if test fails.
        std::ostringstream ss;
        ss << rng;
        auto rng_state = ss.str();
        CAPTURE(rng_state);

        using rand_u32 = std::uniform_int_distribution<uint32_t>;
        using rand_tick = std::uniform_int_distribution<TickT>;
        using rand_bool = std::bernoulli_distribution;

        auto random_doc = [] (RNG & rng) {
            // `beat + 2` can overflow the end of the pattern.
            // Events are misordered, but doc_edited() pretends
            // the new document was always there (and ignores misorderings).
            auto beat = rand_u32{0, 2}(rng);
            auto delay = rand_tick{0, 9}(rng);
            return parametric_doc(beat, delay);
        };

        Document document = random_doc(rng);

        auto pure = make_channel_sequencer(0, 0);
        auto dirty = make_channel_sequencer(0, 0);

        for (int tick = 0; tick < 100; tick++) {
            CAPTURE(tick);
            // Randomly decide whether to switch documents.
            if (rand_bool{0.1}(rng)) {
                document = random_doc(rng);

                // The ground truth is trained on the new document from scratch.
                // Replaying the entire history is O(n^2) but whatever.
                pure = make_channel_sequencer(0, 0);
                for (int j = 0; j < tick; j++) {
                    pure.next_tick(document);
                }

                // The dirty sequencer is informed the document has changed.
                dirty.doc_edited(document);
            }

            // Make sure both sequencers agree.
            auto [pure_time, pure_ev] = pure.next_tick(document);
            auto [dirty_time, dirty_ev] = dirty.next_tick(document);
            CHECK(pure_time == dirty_time);
            CHECK(pure_ev == dirty_ev);
        }
    }
}

#endif

// end namespaces
}
