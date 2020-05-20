#include "sequencer.h"

#include "util/release_assert.h"

#ifdef UNITTEST
#include <doctest.h>
#endif

#include <algorithm>  // std::min
#include <limits>  // std::numeric_limits
#include <type_traits>  // std::is_signed_v

namespace audio::synth::sequencer {

static TickT & tick_or_delay(TickT & self) {
    return self;
}

static TickT & tick_or_delay(TickOrDelayEvent & self) {
    return self.tick_or_delay;
}

// Taken from https://stackoverflow.com/a/28139075
template <typename T>
struct reversion_wrapper { T& iterable; };

template <typename T>
auto begin (reversion_wrapper<T> w) { return std::rbegin(w.iterable); }

template <typename T>
auto end (reversion_wrapper<T> w) { return std::rend(w.iterable); }

template <typename T>
reversion_wrapper<T> reverse (T&& iterable) { return { iterable }; }

/// Each event cannot occur later than events following it.
/// Move each event ahead in time to enforce this rule.
template<typename TickOrDelayT>
static void make_tick_times_monotonic(gsl::span<TickOrDelayT> /*inout*/ tick_times) {
    TickT latest_tick = std::numeric_limits<TickT>::max();

    // Iterate over events in reversed order.
    for (TickOrDelayT &/*mut*/ tick_obj : reverse(tick_times)) {
        TickT & tick = tick_or_delay(tick_obj);

        // Each event cannot occur later than events following it.
        // Move each event ahead in time to enforce this rule.
        tick = latest_tick = std::min(tick, latest_tick);
    }
}

/// Converts a list of times from "absolute time" to "delay from previous time".
///
/// Input:
/// - n = input.tick_times.size()
/// - input.tick_times[i] = timestamp.
/// - input.tick_times is weakly increasing (<= holds).
///     - Call make_tick_times_monotonic() first.
///
/// Return value:
/// - ret_i: [0..n] inclusive
/// - input.tick_times[0..ret_i) < now
/// - input.tick_times[ret_i..n) >= now
///
/// Mutate input:
/// - output.tick_times[0..ret_i) are unspecified.
/// - output.tick_times[ret_i] = input.tick_times[ret_i] - now
/// - output.tick_times[i in [ret_i+1..n)] = input.tick_times[i] - input.tick_times[i-1]
template<typename TickOrDelayT>
static ptrdiff_t convert_tick_to_delay(
    TickT const now, gsl::span<TickOrDelayT> /*inout*/ tick_times
) {
    auto n = tick_times.size();

    ptrdiff_t const IDX_UNSET = -1;
    ptrdiff_t ret_i = IDX_UNSET;

    // If we forget to initialize this variable, the results should be obviously wrong.
    TickT prev = std::numeric_limits<TickT>::max() / 2;

    // If this were Rust, I'd store ret_i and prev in an enum instead,
    // and prev wouldn't need to be initialized to a dummy variable.
    for (ptrdiff_t curr_idx = 0; curr_idx < n; curr_idx++) {
        TickT input = tick_or_delay(tick_times[curr_idx]);
        TickT & output = tick_or_delay(tick_times[curr_idx]);

        if (ret_i == IDX_UNSET && input < now) {
            // input.tick_times[0..ret_i) < now
            // output.tick_times[0..ret_i) are unspecified.
        } else
        if (ret_i == IDX_UNSET && input >= now) {
            // input.tick_times[ret_i..n) >= now
            ret_i = curr_idx;

            // output.tick_times[ret_i] = input.tick_times[ret_i] - now
            output = input - now;
            prev = input;
        } else
        if (ret_i != IDX_UNSET) {
            // output.tick_times[i in [ret_i+1..n)] = input.tick_times[i] - input.tick_times[i-1]
            output = input - prev;
            prev = input;
        } else
            release_assert(false);
    }

    // ret_i: [0, n] inclusive
    if (ret_i == IDX_UNSET) {
        ret_i = n;
    }

    return ret_i;
}

// TODO add support for grooves.
static TickT time_to_ticks(doc::TimeInPattern time, doc::SequencerOptions options) {
    return doc::round_to_int(time.anchor_beat * options.ticks_per_beat)
        + time.tick_offset;
}

// impl FlattenedEventList

using TimedEventsRef = gsl::span<doc::TimedRowEvent const>;
using DelayEvent = TickOrDelayEvent;
using DelayEventsRefMut = gsl::span<DelayEvent>;

// TODO cache all inputs into load_events_mut(),
// and add a method to increment cache.now().

[[nodiscard]] DelayEventsRefMut get_events_mut(FlattenedEventList & self) {
    return DelayEventsRefMut{self._delay_events}.subspan(self._next_event_idx);
}

struct EventListAndDuration {
    TimedEventsRef event_list;
    doc::BeatFraction nbeats;
};

/// Mutates self._delay_events, returns a reference.
[[nodiscard]] DelayEventsRefMut load_events_mut(
    FlattenedEventList & self,
    EventListAndDuration const pattern,
    doc::SequencerOptions options,
    TickT now
) {
    /* TODO add parameter: BeatFraction start_beat.
    - Drop all events prior.
    - Convert all events into ticks.
    - Subtract time_to_ticks(start_beat) from all event times.
    */
    // TODO add EventList parameter for next pattern's "pickup events"???

    TimedEventsRef event_list = pattern.event_list;
    // Is it legal for patterns to hold events with anchor beat > pattern duration?

    self._delay_events.clear();
    self._delay_events.reserve(event_list.size());  // Does not shrink the vector.
    for (doc::TimedRowEvent event : event_list) {
        TickT tick = time_to_ticks(event.time, options);
        self._delay_events.push_back(
            TickOrDelayEvent{.tick_or_delay = tick, .event = event.v}
        );
    }

    // TODO return status saying "misordered events detected", then display error in GUI.
    // The 6502 hardware driver will not handle misordered events properly;
    // make_tick_times_monotonic() is merely a convenience for preview.
    make_tick_times_monotonic(DelayEventsRefMut{self._delay_events});

    // Converts _delay_events from (tick, event) to (delay, event) format.
    self._next_event_idx = convert_tick_to_delay(now, DelayEventsRefMut{self._delay_events});

    // Returns reference to _delay_events.
    return get_events_mut(self);
}

void pop_event(FlattenedEventList & self) {
    self._next_event_idx++;
    release_assert((size_t) self._next_event_idx <= self._delay_events.size());
}

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

doc::MaybeSequenceIndex calc_next_index(
    doc::Document const & document, doc::SequenceIndex seq_index
) {
    // exotracker will have no pattern-jump effects.
    // Instead, each "order entry" has a length field, and a "what to do next" field.

    // If seq entry jumps to another seq index, return that one instead.

    seq_index++;
    if (seq_index >= document.sequence.size()) {
        // If seq entry can halt song afterwards, return -1.
        return {};
    }
    return {seq_index};

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

    doc::SequencerOptions options = document.sequencer_options;
    doc::MaybeSequenceIndex next_index = calc_next_index(document, _curr_seq_index);

    // Process the current sequence entry.
    doc::SequenceEntry const & current_entry = document.sequence[_curr_seq_index];
    doc::BeatFraction nbeats = current_entry.nbeats;
    TickT pattern_ntick = doc::round_to_int(nbeats * options.ticks_per_beat);

    auto & chip_channel_events = current_entry.chip_channel_events;

    // [chip_index]
    release_assert(chip_channel_events.size() == nchip);
    auto & channel_events = chip_channel_events[chip_index];

    // [chip_index][chan_index]
    release_assert(channel_events.size() == nchan);
    doc::EventList const & events = channel_events[chan_index];

    // Load list of upcoming events.
    // (In the future, mutate list instead of regenerating with different `now` every tick.
    // Use assertions to ensure that mutation and regeneration produce the same result.)
    DelayEventsRefMut delay_events = load_events_mut(
        _event_cache, {events, nbeats}, document.sequencer_options, _next_tick
    );

    // Process all events occurring now.
    for (DelayEvent & delay_event : delay_events) {
        if (delay_event.delay() == 0) {
            _events_this_tick.push_back(delay_event.event);
            pop_event(_event_cache);
        } else {
            // This has no effect now, but will be useful
            // once we reuse _event_cache across ticks.
            delay_event.delay() -= 1;
            break;
        }
    }

    _next_tick++;
    if (_next_tick >= pattern_ntick) {
        // This code will make each pattern play for at least 1 tick.
        // Only insane patterns would have lengths rounding down to 0 ticks.
        _next_tick = 0;
        _prev_seq_index = {_curr_seq_index};

        if (next_index.has_value()) {
            _curr_seq_index = *next_index;
        } else {
            // TODO halt playback
            _curr_seq_index = 0;
        }
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

static TickT distance(TickT a, TickT b) {
    return b - a;
}

TEST_CASE("convert_tick_to_delay on an empty input") {
    std::vector<TickT> tick_vec;
    gsl::span<TickT> tick_times{tick_vec};

    CHECK(convert_tick_to_delay(TickT{-100}, tick_times) == 0);
    CHECK(convert_tick_to_delay(TickT{0}, tick_times) == 0);
    CHECK(convert_tick_to_delay(TickT{100}, tick_times) == 0);
}

TEST_CASE("convert_tick_to_delay on dense input") {
    std::vector<TickT> tick_vec{0, 1, 2, 3, 4};
    gsl::span<TickT> tick_times{tick_vec};

    CHECK(convert_tick_to_delay(TickT{2}, tick_times) == 2);
    CHECK(tick_times[2] == 0);
    CHECK(tick_times[3] == 1);
    CHECK(tick_times[4] == 1);
}

TEST_CASE("convert_tick_to_delay with repeated input") {
    std::vector<TickT> tick_vec{0, 1, 2, 2, 2};
    gsl::span<TickT> tick_times{tick_vec};

    CHECK(convert_tick_to_delay(TickT{2}, tick_times) == 2);
    CHECK(tick_times[2] == 0);
    CHECK(tick_times[3] == 0);
    CHECK(tick_times[4] == 0);
}

TEST_CASE("convert_tick_to_delay with gaps") {
    std::vector<TickT> tick_vec{0, 5, 10, 15};
    gsl::span<TickT> tick_times{tick_vec};

    CHECK(convert_tick_to_delay(TickT{7}, tick_times) == 2);
    CHECK(tick_times[2] == 10 - 7);
    CHECK(tick_times[3] == 15 - 10);
}

TEST_CASE("convert_tick_to_delay with negative input") {
    {
        std::vector<TickT> tick_vec{-20, -10, 0};
        gsl::span<TickT> tick_times{tick_vec};

        CHECK(convert_tick_to_delay(TickT{-15}, tick_times) == 1);
        CHECK(tick_times[1] == distance(-15, -10));
        CHECK(tick_times[2] == distance(-10, 0));
    }
    {
        std::vector<TickT> tick_vec{-20, -10, 0};
        gsl::span<TickT> tick_times{tick_vec};

        CHECK(convert_tick_to_delay(TickT{10}, tick_times) == 3);
    }
    {
        std::vector<TickT> tick_vec{0, 10};
        gsl::span<TickT> tick_times{tick_vec};

        CHECK(convert_tick_to_delay(TickT{-10}, tick_times) == 0);
        CHECK(tick_times[0] == distance(-10, 0));
        CHECK(tick_times[1] == distance(0, 10));
    }
}

TEST_CASE("convert_tick_to_delay returns 0") {
    std::vector<TickT> tick_vec{5, 10, 20};
    gsl::span<TickT> tick_times{tick_vec};

    CHECK(convert_tick_to_delay(TickT{0}, tick_times) == 0);
    CHECK(tick_times[0] == distance(0, 5));
    CHECK(tick_times[1] == distance(5, 10));
    CHECK(tick_times[2] == distance(10, 20));
}

TEST_CASE("convert_tick_to_delay returns n") {
    std::vector<TickT> tick_vec{5, 10, 20};
    gsl::span<TickT> tick_times{tick_vec};

    CHECK(convert_tick_to_delay(TickT{30}, tick_times) == 3);
}

#endif

// end namespaces
}
