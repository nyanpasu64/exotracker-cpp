#include "sequencer.h"

#include "util/macros.h"  // release_assert

#ifdef UNITTEST
#include <doctest.h>
#endif

#include <algorithm>  // std::min
#include <cassert>  // assert (not used at the moment)
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

DelayEventsRef FlattenedEventList::load_events_mut(
    EventListAndDuration const pattern,
    doc::SequencerOptions options,
    TickT now
) {
    // TODO add EventList parameter for next pattern's "pickup events"

    doc::EventList const & event_list = pattern.event_list;
    // Is it legal for patterns to hold events with anchor beat > pattern duration?

    _delay_events.clear();
    _delay_events.reserve(event_list.size());  // Does not shrink the vector.
    for (doc::TimedRowEvent event : event_list) {
        TickT tick = time_to_ticks(event.time, options);
        _delay_events.push_back(
            TickOrDelayEvent{.tick_or_delay = tick, .event = event.v}
        );
    }

    make_tick_times_monotonic(DelayEventsRef{_delay_events});

    // Converts _delay_events from (tick, event) to (delay, event) format.
    _next_event_idx = convert_tick_to_delay(now, DelayEventsRef{_delay_events});

    // Returns reference to _delay_events.
    return get_events_mut();
}

DelayEventsRef FlattenedEventList::get_events_mut() {
    return DelayEventsRef{_delay_events}.subspan(_next_event_idx);
}

void FlattenedEventList::pop_event() {
    _next_event_idx++;
    release_assert((size_t) _next_event_idx <= _delay_events.size());
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

EventsRef ChannelSequencer::next_tick(
    doc::Document & document, ChipIndex chip_index, ChannelIndex chan_index
) {
    _events_this_tick.clear();

    doc::BeatFraction nbeats = document.pattern.nbeats;
    doc::SequencerOptions options = document.sequencer_options;
    TickT pattern_ntick = doc::round_to_int(nbeats * options.ticks_per_beat);

    auto const nchip = document.chips.size();
    release_assert(chip_index < nchip);
    auto const nchan = document.chip_index_to_nchan(chip_index);
    release_assert(chan_index < nchan);

    auto & chip_channel_events = document.pattern.chip_channel_events;

    // [chip_index]
    release_assert(chip_channel_events.size() == nchip);
    auto & channel_events = chip_channel_events[chip_index];

    // [chip_index][chan_index]
    release_assert(channel_events.size() == nchan);
    doc::EventList const & events = channel_events[chan_index];

    // Load list of upcoming events.
    // (In the future, mutate list instead of regenerating with different `now` every tick.
    // Use assertions to ensure that mutation and regeneration produce the same result.)
    DelayEventsRef delay_events = _event_cache.load_events_mut(
        {events, nbeats}, document.sequencer_options, _next_tick
    );

    // Process all events occurring now.
    for (DelayEvent & delay_event : delay_events) {
        if (delay_event.delay() == 0) {
            _events_this_tick.push_back(delay_event.event);
            _event_cache.pop_event();
        } else {
            // This has no effect now, but will be useful
            // once we reuse _event_cache across ticks.
            delay_event.delay() -= 1;
            break;
        }
    }

    _next_tick++;
    if (_next_tick >= pattern_ntick) {
        // TODO switch to next pattern.
        _next_tick = 0;
        // This code will make each pattern play for at least 1 tick.
        // Only insane patterns would have lengths rounding down to 0 ticks.
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
