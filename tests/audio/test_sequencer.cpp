#include "audio/synth/sequencer.h"
#include "chip_kinds.h"
#include "timing_common.h"
#include "sample_docs.h"
#include "doc_util/event_builder.h"
#include "doc_util/sample_instrs.h"
#include "test_utils/parameterize.h"

#include <fmt/core.h>
#include <doctest.h>
#include <gsl/span_ext>  // span == span

#include <algorithm>  // std::max
#include <random>

namespace audio::synth::sequencer {

// I found some interesting advice for building comprehensive code tests:
// "Rethinking Software Testing: Perspectives from the world of Hardware"
// https://software.rajivprab.com/2019/04/28/rethinking-software-testing-perspectives-from-the-world-of-hardware/

using namespace doc;
using chip_kinds::ChipKind;
using namespace doc_util::event_builder;
using doc_util::sample_instrs::spc_chip_channel_settings;
using Ev = EventBuilder;
using std::move;

static Document simple_doc() {
    SequencerOptions sequencer_options{
        .ticks_per_beat = 10,
        .beats_per_minute = 100,
    };

    Timeline timeline;

    timeline.push_back([]() -> TimelineRow {
        TimelineCell ch0{TimelineBlock::from_events({
            // TimeInPattern, RowEvent
            {0, {0}},
            {1, {1}},
        })};

        return TimelineRow{
            .nbeats = 2,
            .chip_channel_cells = {{move(ch0), {}, {}, {}, {}, {}, {}, {}}},
        };
    }());

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = Samples(),
        .instruments = Instruments(),
        .chips = {ChipKind::Spc700},
        .chip_channel_settings = spc_chip_channel_settings(),
        .timeline = move(timeline),
    };
}

static ChannelSequencer make_channel_sequencer(
    ChipIndex chip_index, ChannelIndex chan_index, Document const & document
) {
    ChannelSequencer seq;
    seq.set_chip_chan(chip_index, chan_index);
    seq.seek(document, GridAndBeat{});
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
    auto seq = make_channel_sequencer(0, 0, document);

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
    auto seq = make_channel_sequencer(0, 0, document);

    bool reload_doc;
    PICK(should_reload_doc(reload_doc));
    auto next_tick = [&] () {
        if (reload_doc) {
            seq.doc_edited(document);
        }
        return seq.next_tick(document);
    };

    seq.seek(document, GridAndBeat{0, {1, 2}});

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

char const * DOC_NAMES[] {
    "dream-fragments",
//    "world-revolution",
};

TEST_CASE("Ensure sequencer behaves the same with and without reloading position") {
    for (auto doc_name : DOC_NAMES) {
        CAPTURE(doc_name);
        Document const & document = sample_docs::DOCUMENTS.at(doc_name);

        for (
            ChannelIndex chan = 0;
            chan < CHIP_TO_NCHAN[(size_t) ChipKind::Spc700];
            chan++
        ) {
            CAPTURE(chan);

            auto normal = make_channel_sequencer(0, chan, document);
            auto reload = make_channel_sequencer(0, chan, document);

            int ticks = 0;
            int normal_loops = 0;
            int reload_loops = 0;
            while (true) {
                CAPTURE(ticks);

                auto [normal_time, normal_ev] = normal.next_tick(document);
                // CAPTURE() creates a [&] lambda which cannot capture a
                // structured binding.
                auto normal_time_v = normal_time;
                CAPTURE(normal_time_v);

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
                ticks++;
            }
        }
    }
}

TEST_CASE("Reload tempo on every tick, and ensure it doesn't affect behavior") {
    for (auto doc_name : DOC_NAMES) {
        CAPTURE(doc_name);
        Document const & document = sample_docs::DOCUMENTS.at(doc_name);

        for (
            ChannelIndex chan = 0;
            chan < CHIP_TO_NCHAN[(size_t) ChipKind::Spc700];
            chan++
        ) {
            CAPTURE(chan);

            auto normal = make_channel_sequencer(0, chan, document);
            auto reload = make_channel_sequencer(0, chan, document);

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

                reload.tempo_changed(document);
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
/// loop_length = ???
static Document parametric_doc(
    uint32_t beat, TickT delay, int peak_delay, MaybeNonZero<uint32_t> loop_length
) {
    SequencerOptions sequencer_options{
        .ticks_per_beat = 10,
        .beats_per_minute = 100,
    };

    Timeline timeline;

    // grid 0
    timeline.push_back([&]() -> TimelineRow {
        TimelineCell ch0{
            TimelineBlock::from_events({
                // TimeInPattern, RowEvent
                Ev(beat, {0}).delay(delay),
                Ev(beat + 2, {1}).delay(-delay),
            }, loop_length)
        };

        return TimelineRow {
            .nbeats = 4,
            .chip_channel_cells = {{move(ch0), {}, {}, {}, {}, {}, {}, {}}},
        };
    }());

    // The second pattern is ✨different✨.
    delay = peak_delay - delay;

    // grid 1
    timeline.push_back([&]() -> TimelineRow {
        TimelineCell ch0{
            // Add two blocks into one grid cell, as a test case.
            TimelineBlock{(BeatIndex) beat, beat + 2, Pattern{
                {
                    // TimeInPattern, RowEvent
                    Ev(0, {2}).delay(delay),
                },
                loop_length
            }},
            TimelineBlock{(BeatIndex) beat + 2, END_OF_GRID, Pattern{
                {
                    // TimeInPattern, RowEvent
                    Ev(0, {3}).delay(-delay),
                },
                loop_length
            }},
        };

        return TimelineRow {
            .nbeats = 4,
            .chip_channel_cells = {{move(ch0), {}, {}, {}, {}, {}, {}, {}}},
        };
    }());

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = Samples(),
        .instruments = Instruments(),
        .chips = {ChipKind::Spc700},
        .chip_channel_settings = spc_chip_channel_settings(),
        .timeline = move(timeline),
    };
}

/// Single-grid document intended to test for looping bugs.
///
/// nbeat = [0, 4]. Controls how many events are emitted. If 0, document is empty.
/// delay = [-4, 4] or less at fast tempos.
/// loop_length >= nbeat and >= 0.
static Document short_doc(
    uint32_t nbeat, TickT delay, MaybeNonZero<uint32_t> loop_length
) {
    SequencerOptions sequencer_options{
        .ticks_per_beat = 10,
        .beats_per_minute = 100,
    };

    Timeline timeline;

    // grid 0
    timeline.push_back([&]() -> TimelineRow {
        EventList events;
        for (uint32_t beat = 0; beat < nbeat; beat++) {
            events.push_back(Ev(beat, {beat}).delay(delay));
        }

        TimelineCell ch0{TimelineBlock::from_events(move(events), loop_length)};

        return TimelineRow {
            .nbeats = 4,
            .chip_channel_cells = {{move(ch0), {}, {}, {}, {}, {}, {}, {}}},
        };
    }());

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = Samples(),
        .instruments = Instruments(),
        .chips = {ChipKind::Spc700},
        .chip_channel_settings = spc_chip_channel_settings(),
        .timeline = move(timeline),
    };
}

/// Document with many empty grid cells (no blocks).
static Document gap_doc(
    uint32_t nbeat, TickT delay, MaybeNonZero<uint32_t> loop_length
) {
    SequencerOptions sequencer_options{
        .ticks_per_beat = 10,
        .beats_per_minute = 100,
    };

    Timeline timeline;



    EventList events;
    for (uint32_t beat = 0; beat < nbeat; beat++) {
        events.push_back(Ev(beat, {beat}).delay(delay));
    }

    // grid 0
    timeline.push_back([&]() -> TimelineRow {
        TimelineCell ch0{
            TimelineBlock{0, END_OF_GRID, Pattern{std::move(events), loop_length}}
        };

        return TimelineRow {
            .nbeats = 4,
            .chip_channel_cells = {{move(ch0), {}, {}, {}, {}, {}, {}, {}}},
        };
    }());

    // grid 1
    timeline.push_back(TimelineRow {
        .nbeats = 4,
        .chip_channel_cells = {{{}, {}, {}, {}, {}, {}, {}, {}}},
    });

    // grid 2
    timeline.push_back(TimelineRow {
        .nbeats = 4,
        .chip_channel_cells = {{{}, {}, {}, {}, {}, {}, {}, {}}},
    });

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = Samples(),
        .instruments = Instruments(),
        .chips = {ChipKind::Spc700},
        .chip_channel_settings = spc_chip_channel_settings(),
        .timeline = move(timeline),
    };
}

std::random_device rd;

TEST_CASE("Randomly switch between randomly generated documents of the same length") {
    // Keep two sequencers and tick both in lockstep.
    // Occasionally switch documents.
    // Upon each mutation, one sequencer is reset and advanced from scratch,
    // and the other is informed the document has changed.

    // okay i wasted how many hours on this test that caught absolutely nothing
    // and is completely unnecessary given the way I implemented doc_edited()...
    // (doc_edited() ignores our position in the old document, and only keeps _now.)

    // UPDATE: i have underestimated my ability to break existing code during rewrites.

    using RNG = std::minstd_rand;
    RNG rng{rd()};

    for (int i = 0; i < 300; i++) {
        // Capture state if test fails.
        std::ostringstream ss;
        ss << rng;
        auto rng_state = ss.str();
        CAPTURE(rng_state);

        using rand_u32 = std::uniform_int_distribution<uint32_t>;
        using rand_tick = std::uniform_int_distribution<TickT>;
        using rand_bool = std::bernoulli_distribution;

        auto const which_doc = rand_u32{0, 2}(rng);

        bool negative_ticks = false;
        auto random_doc = [which_doc, &negative_ticks] (RNG & rng) {
            switch (which_doc) {
            case 0: {
                // `beat + 2` can overflow the end of the pattern.
                // Events are misordered, but doc_edited() pretends
                // the new document was always there (and ignores misorderings).
                auto begin_beat = rand_u32{0, 2}(rng);
                auto delay = rand_tick{0, 9}(rng);
                auto peak_delay = rand_bool{0.5}(rng) ? 10 : 0;
                auto loop_length = rand_bool{0.5}(rng) ? begin_beat + 2 : 0;
                return parametric_doc(begin_beat, delay, peak_delay, loop_length);

            }
            case 1: {
                auto nbeat = rand_u32{0, 2}(rng);
                auto delay = rand_tick{-4, 4}(rng);
                auto loop_length = rand_u32{std::max(nbeat, 1u), 4}(rng);

                negative_ticks = delay < 0;
                return short_doc(nbeat, delay, loop_length);
            }
            case 2: {
                auto nbeat = rand_u32{0, 2}(rng);
                auto delay = rand_tick{-4, 4}(rng);
                auto loop_length = rand_u32{std::max(nbeat, 1u), 4}(rng);

                negative_ticks = delay < 0;
                return gap_doc(nbeat, delay, loop_length);
            }
            }
            throw std::logic_error("Unknown document type");
        };

        Document document = random_doc(rng);

        auto pure = make_channel_sequencer(0, 0, document);
        auto dirty = make_channel_sequencer(0, 0, document);

        for (int tick = 0; tick < 100; tick++) {
            CAPTURE(tick);
            // Randomly decide whether to switch documents.
            if (rand_bool{0.1}(rng)) {
                /*
                If you add negative delays at the beginning of a document,
                then call doc_edited() before tick 0,
                then only the edited sequencer will skip the negative-delay notes.

                If you reduce the number of events but don't call doc_edited(),
                then the sequencer could crash.

                So don't add negative delays on tick 0.
                (Desync doesn't happen if you edit a document
                to remove negative delays.)
                */

                Document new_doc = random_doc(rng);
                if (!(tick == 0 && negative_ticks)) {
                    document = std::move(new_doc);

                    // The ground truth is trained on the new document from scratch.
                    // Replaying the entire history is O(n^2) but whatever.
                    pure = make_channel_sequencer(0, 0, document);
                    for (int j = 0; j < tick; j++) {
                        pure.next_tick(document);
                    }

                    // The dirty sequencer is informed the document has changed.
                    dirty.doc_edited(document);
                }
            }

            // Make sure both sequencers agree.
            auto [pure_time, pure_ev] = pure.next_tick(document);
            auto [dirty_time, dirty_ev] = dirty.next_tick(document);
            CHECK(pure_time == dirty_time);
            CHECK(pure_ev == dirty_ev);
        }
    }
}

TEST_CASE("Randomly switch between randomly generated documents of different lengths") {
    // Occasionally switch documents, tell the sequencer,
    // and make sure it doesn't crash.
    // We don't compare to ground truth because behavior is ill-defined in some cases,
    // like when switching from a long to a short cell.
    // Behavior will be evaluated through manual testing.

    using RNG = std::minstd_rand;
    RNG rng{rd()};

    // 300 iterations is not enough to expose rare bugs.
    // However, increasing the number of iterations slows down the test.
    for (int i = 0; i < 300; i++) {
        // Capture state if test fails.
        std::ostringstream ss;
        ss << rng;
        auto rng_state = ss.str();
        CAPTURE(rng_state);

        using rand_u32 = std::uniform_int_distribution<uint32_t>;
        using rand_tick = std::uniform_int_distribution<TickT>;
        using rand_bool = std::bernoulli_distribution;

        struct WhichDoc {
            Document document;
            uint32_t which_doc;
            bool negative_ticks;
        };

        auto random_doc = [] (RNG & rng) -> WhichDoc {
            Document document{{}};
            uint32_t const which_doc = rand_u32{0, 2}(rng);
            bool negative_ticks;

            switch (which_doc) {
            case 0: {
                // `beat + 2` can overflow the end of the pattern.
                // Events are misordered, but doc_edited() pretends
                // the new document was always there (and ignores misorderings).
                auto begin_beat = rand_u32{0, 2}(rng);
                auto delay = rand_tick{0, 9}(rng);
                auto peak_delay = rand_bool{0.5}(rng) ? 10 : 0;
                auto loop_length = rand_bool{0.5}(rng) ? begin_beat + 2 : 0;

                negative_ticks = false;
                document = parametric_doc(begin_beat, delay, peak_delay, loop_length);
                break;
            }
            case 1: {
                auto nbeat = rand_u32{0, 2}(rng);
                auto delay = rand_tick{-4, 4}(rng);
                auto loop_length = rand_u32{std::max(nbeat, 1u), 4}(rng);

                negative_ticks = delay < 0;
                document = short_doc(nbeat, delay, loop_length);
                break;
            }
            case 2: {
                auto nbeat = rand_u32{0, 2}(rng);
                auto delay = rand_tick{-4, 4}(rng);
                auto loop_length = rand_u32{std::max(nbeat, 1u), 4}(rng);

                negative_ticks = delay < 0;
                document = gap_doc(nbeat, delay, loop_length);
                break;
            }
            default:
                throw std::logic_error("Unknown document type");
            }

            return WhichDoc{move(document), which_doc, negative_ticks};
        };

        auto curr = random_doc(rng);

        auto seq = make_channel_sequencer(0, 0, curr.document);

        for (int tick = 0; tick < 100; tick++) {
            CAPTURE(tick);
            // Randomly decide whether to switch documents.
            if (rand_bool{0.1}(rng)) {
                /*
                If you add negative delays at the beginning of a document,
                then call doc_edited() before tick 0,
                then only the edited sequencer will skip the negative-delay notes.

                If you reduce the number of events but don't call doc_edited(),
                then the sequencer could crash.

                So don't add negative delays on tick 0.
                (Desync doesn't happen if you edit a document
                to remove negative delays.)
                */

                // mutable state is hard to reason about.
                // We decide whether to switch documents or not,
                // based on the new value of random_doc().
                // And if we choose to not switch documents, we must not alter curr.
                auto prev_which_doc = curr.which_doc;
                auto next = random_doc(rng);
                if (!(tick == 0 && next.negative_ticks)) {
                    curr = move(next);

                    if (curr.which_doc != prev_which_doc) {
                        seq.tempo_changed(curr.document);
                        seq.timeline_modified(curr.document);
                    } else {
                        seq.doc_edited(curr.document);
                    }
                }
            }

            // Make sure both sequencers agree.
            seq.next_tick(curr.document);
        }
    }
}

TEST_CASE("Deterministically switch between tempos") {
    // Seed 1716136822 in the below random test.
    Document document = simple_doc();

    auto seq = make_channel_sequencer(0, 0, document);

    for (int tick = 0; tick < 500; tick++) {
        CAPTURE(tick);
        // Randomly decide whether to switch documents.
        if (tick == 1) {
            // This frequently causes `release_assert(dbeat <= 1)` to fail.
            document.sequencer_options.ticks_per_beat = 6;
            seq.tempo_changed(document);
        }
        if (tick == 3) {
            document.sequencer_options.ticks_per_beat = 1;
            seq.tempo_changed(document);
        }

        seq.next_tick(document);
    }
}

TEST_CASE("Switch tempos twice on every tick, and ensure it doesn't affect behavior") {
    // Keep two sequencers and tick both in lockstep.
    // Occasionally, double "ticks per beat" and set it back in a single tick.

    // If ChannelSequencer::tempo_changed() is implemented improperly,
    // this changes the time of the sequencer.
    // Unfortunately, the current fix causes each call to tempo_changed()
    // to round off _now.next_tick, and the next call to use the rounded value.
    // This is considered acceptable, so doubling "ticks per beat"
    // (instead of a fractional multiplier)
    // prevents rounding errors from failing the test.

    // Can this bug occur in real life? Yes.
    // OverallSynth's current design coalesces all tempo changes on the same callback,
    // but multiple callbacks can occur without an intervening tick.

    for (auto doc_name : DOC_NAMES) {
        CAPTURE(doc_name);
        Document const& doc = sample_docs::DOCUMENTS.at(doc_name);

        Document slow_doc = doc.clone();
        slow_doc.sequencer_options.ticks_per_beat *= 2;

        auto pure = make_channel_sequencer(0, 0, doc);
        auto dirty = make_channel_sequencer(0, 0, doc);

        for (int tick = 0; tick < 100; tick++) {
            CAPTURE(tick);
            // Randomly decide whether to switch documents.

            dirty.tempo_changed(slow_doc);
            dirty.tempo_changed(doc);

            // Make sure both sequencers agree.
            auto [pure_time, pure_ev] = pure.next_tick(doc);
            auto [dirty_time, dirty_ev] = dirty.next_tick(doc);
            REQUIRE(pure_time == dirty_time);
            CHECK(pure_ev == dirty_ev);
        }
    }
}


TEST_CASE("Randomly switch between random tempos") {
    // Maybe my random test architecture from last time wasn't wasted.

    using RNG = std::minstd_rand;
    RNG rng{rd()};

    bool reload_doc;
    PICK(should_reload_doc(reload_doc));

    for (int i = 0; i < 300; i++) {
        // Capture state if test fails.
        std::ostringstream ss;
        ss << rng;
        auto rng_state = ss.str();
        CAPTURE(rng_state);

        using rand_u32 = std::uniform_int_distribution<uint32_t>;
        using rand_tick = std::uniform_int_distribution<TickT>;
        using rand_bool = std::bernoulli_distribution;

        auto const which_doc = rand_u32{0, 2}(rng);

        auto random_doc = [which_doc] (RNG & rng) {
            switch (which_doc) {
            case 0: {
                auto begin_beat = rand_u32{0, 2}(rng);
                auto delay = rand_tick{0, 1}(rng);
                auto peak_delay = 1;
                auto loop_length = rand_bool{0.5}(rng) ? begin_beat + 2 : 0;
                return parametric_doc(begin_beat, delay, peak_delay, loop_length);

            }
            case 1: {
                auto nbeat = rand_u32{0, 2}(rng);
                auto delay = rand_tick{-1, 1}(rng);
                auto loop_length = rand_u32{std::max(nbeat, 1u), 4}(rng);

                return short_doc(nbeat, delay, loop_length);
            }
            case 2: {
                auto nbeat = rand_u32{0, 2}(rng);
                auto delay = rand_tick{-1, 1}(rng);
                auto loop_length = rand_u32{std::max(nbeat, 1u), 4}(rng);

                return gap_doc(nbeat, delay, loop_length);
            }
            }
            throw std::logic_error("Unknown document type");
        };

        Document document = random_doc(rng);

        auto seq = make_channel_sequencer(0, 0, document);

        for (int tick = 0; tick < 500; tick++) {
            CAPTURE(tick);
            // Randomly decide how many times to switch documents.
            // Switching multiple times on the same tick is allowed,
            // and could expose bugs (I don't know of any yet).
            if (rand_bool{0.4}(rng)) {
                if (rand_bool{0.25}(rng)) {
                    // This frequently causes `release_assert(dbeat <= 1)` to fail.
                    document.sequencer_options.ticks_per_beat = 1;
                    seq.tempo_changed(document);
                } else {
                    document.sequencer_options.ticks_per_beat =
                        (TickT) rand_u32{2, 10}(rng);
                    seq.tempo_changed(document);
                }
            }

            // There was a bug where changing the tempo
            // and editing the doc on the same tick causes the sequencer to crash.
            // Exercise this edge case too.
            if (reload_doc) seq.doc_edited(document);

            // TODO add way for sequencers to report misordered events to caller.
            seq.next_tick(document);
        }
    }
}

}
