#include "sample_docs.h"

#include "doc.h"
#include "chip_kinds.h"
#include "doc_util/event_builder.h"
#include "doc_util/sample_instrs.h"

#include <cstdint>
#include <cmath>

namespace sample_docs {

using namespace doc;
using namespace doc_util::event_builder;
using namespace doc_util::sample_instrs;
using Ev = EventBuilder;
using std::move;
using std::nullopt;

/// Empty document with one grid cell.
/// Channel 0 has a block/pattern without events, and Channel 1 has no pattern.
///
/// Use as a template for porting other documents.
static Document empty() {
    SequencerOptions sequencer_options{.target_tempo = 150, .ticks_per_beat = 48};

    constexpr SampleIndex TRIANGLE = 0;
    constexpr SampleIndex PULSE_25 = 1;
    constexpr SampleIndex PULSE_50 = 2;

    Samples samples;
    samples[TRIANGLE] = triangle();
    samples[PULSE_25] = pulse_25();
    samples[PULSE_50] = pulse_50();

    Instruments instruments;
    instruments[0] = Instrument{"blank", {}};
    instruments[1] = music_box(TRIANGLE);
    instruments[2] = Instrument{
        .name = "25%",
        .keysplit = { InstrumentPatch { .sample_idx = PULSE_25, .adsr = INFINITE }},
    };
    instruments[3] = Instrument{
        .name = "Keysplit",
        .keysplit = {
            InstrumentPatch {
                .min_note = 0,
                .sample_idx = PULSE_25,
                .adsr = INFINITE,
            },
            InstrumentPatch {
                .min_note = 60,
                .sample_idx = PULSE_50,
                .adsr = INFINITE,
            },
            InstrumentPatch {
                .min_note = 72,
                .sample_idx = TRIANGLE,
                .adsr = INFINITE,
            },
        },
    };
    instruments[0x10] = Instrument{
        .name = "50%",
        .keysplit = { InstrumentPatch { .sample_idx = PULSE_50, .adsr = INFINITE }},
    };

    ChipList chips{ChipKind::Spc700};

    ChipChannelSettings chip_channel_settings = spc_chip_channel_settings();

    Timeline timeline;

    timeline.push_back([]() -> TimelineRow {
        TimelineCell ch0{};

        TimelineCell ch1{TimelineBlock::from_events({
            // Events go here.
        })};

        TimelineCell ch2{TimelineBlock{0, BeatOrEnd(8),
            Pattern{EventList{
                // Events go here.
            }}
        }};

        TimelineCell ch3{TimelineBlock{0, END_OF_GRID,
            Pattern{EventList{
                // Events go here.
            }, 4}
        }};

        return TimelineRow{
            .nbeats = 16,
            .chip_channel_cells = {{move(ch0), move(ch1), move(ch2), move(ch3), {}, {}, {}, {}}},
        };
    }());

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = move(samples),
        .instruments = move(instruments),
        .chips = move(chips),
        .chip_channel_settings = move(chip_channel_settings),
        .timeline = move(timeline),
    };
}

/// Excerpt from "Chrono Cross - Dream Fragments".
/// This tests the ability to nudge notes both early and later,
/// and even before the beginning of a pattern.
static Document dream_fragments() {
    // Global options
    SequencerOptions sequencer_options{
        .target_tempo = 84,
    };

    Samples samples;
    samples[0] = triangle();

    Instruments instruments;
    instruments[0] = music_box(0);

    auto const chip_kind = ChipKind::Spc700;
    ChipList chips{chip_kind};

    ChipChannelSettings chip_channel_settings = spc_chip_channel_settings();

    Timeline timeline;

    timeline.push_back([]() -> TimelineRow {
        TimelineCell ch0{TimelineBlock::from_events({
            // Since ch0 has only 1 effect column,
            // the delay should neither be visible on-screen
            // nor affect the sequencer.
            // TODO write a unit test to make sure the sequencer only uses
            // in-bounds delays.
            Ev(0, pitch(5, 7)).instr(0).no_effect().delay(16),
            Ev(1, pitch(6, 2)),
            Ev(4+0, pitch(5, 7+2)),
            Ev(4+1, pitch(6, 2+2)),
        })};
        TimelineCell ch1{TimelineBlock::from_events({
            Ev(at(0, 3, 4), NOTE_CUT),
            Ev(at(1, 1, 2), pitch(7, -3)).instr(0),
            Ev(at(2, 0, 2), pitch(7, 6)),
            Ev(at(2, 1, 2), pitch(7, 7)),
            Ev(at(3, 1, 2), pitch(7, 9)),
            Ev(at(4, 1, 2), pitch(7, 4)),
            Ev(at(5, 1, 2), pitch(7, 2)),
            Ev(at(6, 1, 2), pitch(7, 1)),
        })};
        return TimelineRow{
            .nbeats = 8,
            .chip_channel_cells = {{move(ch0), move(ch1), {}, {}, {}, {}, {}, {}}},
        };
    }());

    timeline.push_back([] {
        auto ch0 = TimelineCell{TimelineBlock::from_events({
            Ev(0, pitch(6, 4)).instr(0).delay(-5),
            Ev(1, pitch(6, -1)),
            Ev(2, pitch(6, 4)),
            Ev(3, pitch(6, 7)),
            Ev(4, pitch(6, 6)).delay(-5),
            Ev(5, pitch(6, 1)),
            Ev(6, pitch(6, -2)),
            Ev(7, pitch(6, 1)),
        })};
        auto ch1 = TimelineCell{TimelineBlock::from_events({
            Ev(0, pitch(6, 7)).instr(0).delay(-2),
            Ev(at(1, 1, 2), pitch(7, -1)),
            Ev(3, pitch(7, 4)),
            Ev(4, pitch(7, -2)).delay(-2),
            Ev(at(5, 2, 4), pitch(7, 7)),
            Ev(at(5, 3, 4), pitch(7, 6)),
            Ev(6, pitch(7, 4)),
        })};
        TimelineCell ch2{TimelineBlock::from_events({
            Ev(0, pitch(7, -1)).instr(0).delay(1),
            Ev(4, pitch(7, 1)).delay(1),
        })};
        TimelineCell ch3{TimelineBlock::from_events({
            Ev(0, pitch(7, 4)).instr(0).no_effect().delay(4),
            Ev(4, pitch(7, 6)).no_effect().delay(4),
        })};
        return TimelineRow{
            .nbeats = 8,
            .chip_channel_cells = {{move(ch0), move(ch1), move(ch2), move(ch3), {}, {}, {}, {}}},
        };
    }());

    return DocumentCopy {
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = move(samples),
        .instruments = move(instruments),
        .chips = move(chips),
        .chip_channel_settings = chip_channel_settings,
        .timeline = move(timeline),
    };
}

/// Test all 8 channels to make sure they play properly.
static Document all_channels() {
    // Global options
    SequencerOptions sequencer_options{
        .target_tempo = 84,
    };

    Samples samples;
    samples[0] = triangle();

    Instruments instruments;
    instruments[0] = music_box(0);

    auto const chip_kind = ChipKind::Spc700;
    ChipList chips{chip_kind};

    ChipChannelSettings chip_channel_settings = spc_chip_channel_settings();

    Timeline timeline;

    timeline.push_back([]() -> TimelineRow {
        std::vector<TimelineCell> channels;
        for (int i = 0; i < 8; i++) {
            channels.push_back({TimelineBlock::from_events({
                Ev(BeatFraction(i, 4), Note(Chromatic(60 + i))).instr(0)
            })});
        }
        return TimelineRow{
            .nbeats = 8,
            .chip_channel_cells = {move(channels)},
        };
    }());

    return DocumentCopy {
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = move(samples),
        .instruments = move(instruments),
        .chips = move(chips),
        .chip_channel_settings = chip_channel_settings,
        .timeline = move(timeline),
    };
}

#if 0

/// Excerpt from "Chrono Trigger - World Revolution".
/// This tests multiple sequence entries (patterns) of uneven lengths.
static Document world_revolution() {
    SequencerOptions sequencer_options{.ticks_per_beat = 23};

    Instruments instruments;

    constexpr InstrumentIndex BASS = 0;
    instruments[BASS] = Instrument {
        .name = "Bass",
        .volume = {{7, 7, 7, 7, 7, 3}},
        .pitch = {{}},
        .arpeggio = {{}},
        .wave_index = {{1, 1, 1, 0}},
    };

    constexpr InstrumentIndex TRUMPET = 1;
    instruments[TRUMPET] = Instrument {
        .name = "Trumpet",
        .volume = {{5, 6, 7, 8, 8, 9}},
        .pitch = {{}},
        .arpeggio = {{}},
        .wave_index = {{1, 1, 0}},
    };

    constexpr InstrumentIndex TAMBOURINE = 2;
    instruments[TAMBOURINE] = Instrument {
        .name = "Tambourine",
        .volume = {{15, 15, 12, 10, 8, 6, 5, 8, 4, 2, 6, 4, 2, 4, 2, 1, 2, 1, 0, 2, 1, 0, 1, 0, 0}},
        .wave_index = {{0, 1}},
    };

    ChipList chips{ChipKind::Nes};
    ChipChannelSettings chip_channel_settings{{{}, {}, {}, {}, {}}};

    Timeline timeline;

    auto generate_bass = [&](int nbeats, int offset = 5) -> EventList {
        EventList out;
        for (int beat = 0; beat < nbeats; beat++) {
            int note = (beat / 4 % 2 == 0)
                ? pitch(0, offset).value
                : pitch(0, offset + 2).value;
            out.push_back(Ev(beat, pitch(3, note)));
            out.push_back(Ev(at(beat, 1, 4), pitch(3, note + 7)));
            out.push_back(Ev(at(beat, 2, 4), pitch(3, note + 12)));
            out.push_back(Ev(at(beat, 3, 4), pitch(3, note + 7)));
        }

        out[0].v.instr = BASS;

        out[0].v.effects[0] = Effect("AA", 0);
        out[1].v.effects[0] = Effect("II", 0);
        out[2].v.effects[0] = Effect("AI", 0);
        out[3].v.effects[0] = Effect("IA", 0);

        return out;
    };

    constexpr Note NOISE_HIGH = 0xD;
    constexpr Note NOISE_LOW = 0xC;

    auto noise_loop = TimelineCell{TimelineBlock::from_events({
        Ev(0, NOISE_HIGH).instr(TAMBOURINE),
        Ev(at(0, 1, 2), NOISE_LOW),
        Ev(at(1, 1, 2), NOISE_HIGH),
        Ev(at(2, 1, 2), NOISE_LOW),
        Ev(4, NOISE_HIGH),
        Ev(at(4, 1, 2), NOISE_LOW),
        Ev(at(5, 1, 2), NOISE_HIGH),
        Ev(at(6, 1, 2), NOISE_LOW),
        Ev(7, NOISE_LOW),
    }, 8)};

    auto dpcm_level = TimelineCell{TimelineBlock::from_events({
        Ev(0, {}).volume(0x7f)
    })};

    timeline.push_back([&]() -> TimelineRow {
        // Add two blocks into one grid cell, as a test case.
        auto ch0 = TimelineCell{
            TimelineBlock{0, BeatOrEnd(8),
                Pattern{EventList{
                    // 0
                    Ev(0, pitch(6, 0)).instr(TRUMPET).volume(0xf).effect("0A", 0),
                    Ev(at(0, 4, 8), pitch(6, -1)).effect("0Q", 0),
                    Ev(at(0, 6, 8), pitch(6, 0)),
                    Ev(at(0, 7, 8), pitch(6, -1)),
                    Ev(1, pitch(5, 9)),
                    Ev(at(1, 1, 2), pitch(5, 7)),
                    Ev(2, pitch(5, 9)),
                    Ev(at(2, 1, 2), pitch(5, 2)),
                    Ev(3, pitch(5, 4)),
                    Ev(at(3, 1, 2), pitch(5, 7)),
                    // 4
                    Ev(4, pitch(5, 9)),
                    Ev(6, pitch(5, 7)),
                    Ev(7, pitch(5, 9)),
                    Ev(at(7, 1, 2), pitch(6, -1)),
                }}
            },
            TimelineBlock{8, BeatOrEnd(16),
                Pattern{EventList{
                    // 0
                    Ev(0, pitch(6, 0)).volume(0xf),
                    Ev(at(0, 4, 8), pitch(6, -1)),
                    Ev(at(0, 6, 8), pitch(6, 0)),
                    Ev(at(0, 7, 8), pitch(6, -1)),
                    Ev(1, pitch(5, 9)),
                    Ev(at(1, 1, 2), pitch(5, 7)),
                    Ev(2, pitch(5, 9)),
                    Ev(at(2, 1, 2), pitch(6, -1)),
                    Ev(3, pitch(6, 0)),
                    Ev(at(3, 1, 2), pitch(6, 2)),
                    // 4
                    Ev(4, pitch(6, 4)),
                    Ev(6, pitch(6, 2)),
                    Ev(7, pitch(5, 9)),
                    Ev(at(7, 1, 2), pitch(6, -1)),
                }},
            },
        };

        auto ch1 = TimelineCell{
            TimelineBlock{0, 4, Pattern{generate_bass(1, 5), 1}},
            TimelineBlock{4, 8, Pattern{generate_bass(1, 7), 1}},
            TimelineBlock{8, 12, Pattern{generate_bass(1, 5), 1}},
            TimelineBlock{12, 16, Pattern{generate_bass(1, 7), 1}},
        };

        auto tri = TimelineCell{
            TimelineBlock{0, 8, {{
                // 0
                Ev(0, pitch(5, 9)),
                Ev(at(3, 2, 4), pitch(5, 7)),
                Ev(at(3, 3, 4), pitch(5, 9)),
                Ev(4, pitch(5, 7)),
                Ev(6, pitch(5, 4)),
            }}},
            TimelineBlock{8, 16, {{
                // 8
                Ev(0, pitch(5, 9)),
                Ev(at(3, 2, 4), pitch(5, 7)),
                Ev(at(3, 3, 4), pitch(5, 9)),
                Ev(4, pitch(6, 0)),
                Ev(6, pitch(6, -1)),
            }}},
        };

        return TimelineRow{
            .nbeats = 16,
            .chip_channel_cells = {{
                move(ch0), move(ch1), move(tri), noise_loop, dpcm_level
            }},
        };
    }());
    timeline.push_back([&]() -> TimelineRow {
        auto ch0 = TimelineCell{TimelineBlock::from_events({
            // 0
            Ev(0, pitch(6, 0)).instr(TRUMPET),
            Ev(at(0, 4, 8), pitch(6, -1)),
            Ev(at(0, 6, 8), pitch(6, 0)),
            Ev(at(0, 7, 8), pitch(6, -1)),
            Ev(1, pitch(5, 9)),
            Ev(at(1, 1, 2), pitch(5, 7)),
            Ev(2, pitch(5, 9)),
            Ev(at(2, 1, 2), pitch(5, 2)),
            Ev(3, pitch(5, 4)),
            Ev(at(3, 1, 2), pitch(5, 7)),
            // 4
            Ev(4, pitch(5, 9)),
            Ev(5, pitch(6, -1)),
            Ev(6, pitch(6, 0)),
            Ev(7, pitch(6, 2)),
        })};
        auto ch1 = TimelineCell{TimelineBlock::from_events(generate_bass(8))};

        auto tri = TimelineCell{
            TimelineBlock{0, 8, {{
                // 0
                Ev(0, pitch(5, 9)),
                Ev(at(3, 2, 4), pitch(5, 7)),
                Ev(at(3, 3, 4), pitch(5, 9)),
                Ev(4, pitch(5, 7)),
                Ev(6, pitch(5, 4)),
            }}},
        };

        return TimelineRow {
            .nbeats = 8,
            .chip_channel_cells = {{
                move(ch0), move(ch1), move(tri), noise_loop, dpcm_level
            }},
    };
    }());

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = move(instruments),
        .chips = move(chips),
        .chip_channel_settings = move(chip_channel_settings),
        .timeline = move(timeline),
    };
}

/// Test song, populated with note cuts, releases, and stuff.
static Document render_test() {
    SequencerOptions sequencer_options{.ticks_per_beat = 24};

    Instruments instruments;

    char const* names[] = {"12.5%", "25%", "50%"};
    for (size_t i = 0; i <= 2; i++) {
        instruments[i] = Instrument {
            .name = names[i],
            .volume = {{15}},
            .pitch = {{}},
            .arpeggio = {{}},
            .wave_index = {{int8_t(i)}},
        };
    }

    ChipList chips{ChipKind::Apu1};
    ChipChannelSettings chip_channel_settings{{{}, {}}};

    Timeline timeline;

    timeline.push_back([]() -> TimelineRow {
        TimelineCell ch0{TimelineBlock::from_events([&] {
            EventList ev;
            for (int i = 0; i <= 10; i++) {
                // Play MIDI pitches 0, 12... 120.
                ev.push_back(
                    Ev(at(i, 0, 2), pitch(i, 0))
                        .delay(4 * (i - 5))
                );
                ev.push_back(
                    Ev(at(i, 1, 2), {i % 2 == 0 ? NOTE_CUT : NOTE_RELEASE})
                        .delay(4 * (i - 5))
                );
            }
            ev[0].v.instr = 2;
            return ev;
        }())};

        TimelineCell ch1{TimelineBlock::from_events({
            Ev(2, {NOTE_CUT}),
            Ev(4, {NOTE_RELEASE}),
        })};

        return TimelineRow{
            .nbeats = 10 + 1,
            .chip_channel_cells = {{move(ch0), move(ch1)}},
        };
    }());

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = move(instruments),
        .chips = move(chips),
        .chip_channel_settings = move(chip_channel_settings),
        .timeline = move(timeline),
    };
}

/// Test song, plays four notes from two chips nonstop.
/// Check for audio underruns (crackling) by recording and viewing in a spectrogram.
static Document audio_test() {
    SequencerOptions sequencer_options{.ticks_per_beat = 24};

    Instruments instruments;
    instruments[0] = Instrument {
        .name = "50%",
        .volume = {{15}},
        .pitch = {{}},
        .arpeggio = {{}},
        .wave_index = {{2}},
    };

    ChipList chips{ChipKind::Apu1, ChipKind::Apu1};
    ChipChannelSettings chip_channel_settings{{{}, {}}, {{}, {}}};

    auto get_channel = [&] (Note note) {
        return TimelineCell{TimelineBlock::from_events({
            Ev(0, note).instr(0)
        })};
    };

    Timeline timeline;

    timeline.push_back([&]() -> TimelineRow {
        TimelineCell ch0{};

        TimelineCell ch1{TimelineBlock::from_events({
            // Events go here.
        })};

        TimelineCell ch2{TimelineBlock{0, BeatOrEnd(8),
            Pattern{EventList{
                // Events go here.
            }}
        }};

        TimelineCell ch3{TimelineBlock{0, END_OF_GRID,
            Pattern{EventList{
                // Events go here.
            }, 4}
        }};

        return TimelineRow{
            .nbeats = 4,
            .chip_channel_cells = {
                {get_channel(pitch(5, 0)), get_channel(pitch(5, 4))},
                {get_channel(pitch(5, 8)), get_channel(pitch(5, 12))}
            },
        };
    }());

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = move(instruments),
        .chips = move(chips),
        .chip_channel_settings = move(chip_channel_settings),
        .timeline = move(timeline),
    };
}

/// Document used to test block rendering and cursor movement,
/// and see how blocks look when overlapping
static Document block_test() {
    SequencerOptions sequencer_options{.ticks_per_beat = 24};

    Instruments instruments;
    instruments[0] = music_box();

    ChipList chips{ChipKind::Apu1, ChipKind::Apu1};
    ChipChannelSettings chip_channel_settings{{{}, {}}, {{}, {}}};

    TimelineBlock unlooped{0, END_OF_GRID, Pattern{{}}};
    TimelineBlock looped{0, END_OF_GRID, Pattern{{}, 1}};

    std::vector<TimelineCell> ch0;
    ch0.push_back({unlooped});
    ch0.push_back({});
    ch0.push_back({looped});
    ch0.push_back({});
    ch0.push_back({unlooped});

    std::vector<TimelineCell> ch1;
    ch1.push_back({});
    ch1.push_back({looped});
    ch1.push_back({looped});
    ch1.push_back({unlooped});
    ch1.push_back({});

    Timeline timeline;
    for (size_t i = 0; i < ch0.size(); i++) {
        timeline.push_back(TimelineRow{
            .nbeats = 2,
            .chip_channel_cells = {{move(ch0[i]), move(ch1[i])}, {{}, {}}},
        });
    }

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = move(instruments),
        .chips = move(chips),
        .chip_channel_settings = move(chip_channel_settings),
        .timeline = move(timeline),
    };
}

#endif

std::string const DEFAULT_DOC = "all-channels";

std::map<std::string, doc::Document> const DOCUMENTS = [] {
    std::map<std::string, doc::Document> out;
    out.insert({"empty", empty()});
    out.insert({"dream-fragments", dream_fragments()});
    out.insert({"all-channels", all_channels()});
    // out.insert({"world-revolution", world_revolution()});
    // out.insert({"render-test", render_test()});
    // out.insert({"audio-test", audio_test()});
    // out.insert({"block-test", block_test()});
    return out;
}();

}
