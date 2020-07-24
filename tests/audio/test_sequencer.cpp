#include "audio/synth/sequencer.h"
#include "chip_kinds.h"
#include "timing_common.h"
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

namespace audio::synth::sequencer {

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
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = Instruments(),
        .chips = {ChipKind::Apu1},
        .sequence = sequence
    };
}

static ChannelSequencer make_channel_sequencer(
    ChipIndex chip_index, ChannelIndex chan_index, Document const & document
) {
    ChannelSequencer seq;
    seq.set_chip_chan(chip_index, chan_index);
    seq.seek(document, PatternAndBeat{});
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

TEST_CASE("Ensure sequencer behaves the same with and without reloading tempo") {
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
        .accidental_mode = AccidentalMode::Sharp,
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

        auto pure = make_channel_sequencer(0, 0, document);
        auto dirty = make_channel_sequencer(0, 0, document);

        for (int tick = 0; tick < 100; tick++) {
            CAPTURE(tick);
            // Randomly decide whether to switch documents.
            if (rand_bool{0.1}(rng)) {
                document = random_doc(rng);

                // The ground truth is trained on the new document from scratch.
                // Replaying the entire history is O(n^2) but whatever.
                pure = make_channel_sequencer(0, 0, document);
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

TEST_CASE("Randomly switch between random tempos") {
    // Maybe my random test architecture from last time wasn't wasted.

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

        auto seq = make_channel_sequencer(0, 0, document);

        for (int tick = 0; tick < 500; tick++) {
            CAPTURE(tick);
            // Randomly decide whether to switch documents.
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

            // TODO add way for sequencers to report misordered events to caller.
            seq.next_tick(document);
        }
    }
}

}
