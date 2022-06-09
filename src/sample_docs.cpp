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

static Sequence empty_sequence() {
    Sequence sequence{{{}, {}, {}, {}, {}, {}, {}, {}}};

    // Add an empty block to channel 0, so playback (modulo song length) moves within
    // the block rather than hanging in place.
    sequence[0][0].blocks.push_back(TrackBlock::from_events(0, 4 * 48, {}));

    return sequence;
}

Document new_document() {
    SequencerOptions sequencer_options{.target_tempo = 150, .ticks_per_beat = 48};

    constexpr SampleIndex PULSE_12_5 = 0;
    constexpr SampleIndex PULSE_25 = 1;
    constexpr SampleIndex PULSE_50 = 2;
    constexpr SampleIndex TRIANGLE = 3;

    Samples samples;
    samples[PULSE_12_5] = pulse_12_5();
    samples[PULSE_25] = pulse_25();
    samples[PULSE_50] = pulse_50();
    samples[TRIANGLE] = triangle();

    Instruments instruments;
    instruments[0] = Instrument{
        .name = "25%",
        .keysplit = { InstrumentPatch { .sample_idx = PULSE_25, .adsr = INFINITE }},
    };

    ChipList chips{ChipKind::Spc700};

    Sequence sequence = empty_sequence();

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = move(samples),
        .instruments = move(instruments),
        .chips = move(chips),
        .sequence = move(sequence),
    };
}

/// Empty document with one grid cell and test samples/instruments.
/// Channel 0 has a block/pattern without events, and Channel 1 has no pattern.
///
/// Use as a template for porting other documents.
static Document instruments() {
    SequencerOptions sequencer_options{.target_tempo = 150, .ticks_per_beat = 48};

    constexpr SampleIndex TRIANGLE = 0;
    constexpr SampleIndex PULSE_12_5 = 1;
    constexpr SampleIndex PULSE_25 = 2;
    constexpr SampleIndex PULSE_50 = 3;
    constexpr SampleIndex SAW = 4;
    constexpr SampleIndex NOISE = 5;
    constexpr SampleIndex LONG = 6;

    Samples samples;
    samples[TRIANGLE] = triangle();
    samples[PULSE_12_5] = pulse_12_5();
    samples[PULSE_25] = pulse_25();
    samples[PULSE_50] = pulse_50();
    samples[SAW] = saw();
    samples[NOISE] = periodic_noise();
    samples[LONG] = long_silence();

    Instruments instruments;
    instruments[0] = music_box(TRIANGLE);
    instruments[1] = Instrument{
        .name = "12.5%",
        .keysplit = { InstrumentPatch { .sample_idx = PULSE_12_5, .adsr = INFINITE }},
    };
    instruments[2] = Instrument{
        .name = "25%",
        .keysplit = { InstrumentPatch { .sample_idx = PULSE_25, .adsr = INFINITE }},
    };
    instruments[3] = Instrument{
        .name = "50%",
        .keysplit = { InstrumentPatch { .sample_idx = PULSE_50, .adsr = INFINITE }},
    };
    instruments[4] = Instrument{
        .name = "Keysplit",
        .keysplit = {
            InstrumentPatch {
                .min_note = 0,
                .sample_idx = SAW,
                .adsr = DEMO,
            },
            InstrumentPatch {
                .min_note = 60,
                .sample_idx = PULSE_25,
                .adsr = MUSIC_BOX,
            },
            InstrumentPatch {
                .min_note = 72,
                .sample_idx = PULSE_50,
                .adsr = INFINITE,
            },
        },
    };
    instruments[5] = Instrument{
        .name = "Periodic Noise",
        .keysplit = { InstrumentPatch { .sample_idx = NOISE, .adsr = INFINITE }},
    };
    instruments[0x10] = Instrument{"blank", {}};
    instruments[0x11] = Instrument{
        .name = samples[LONG].value().name,
        .keysplit = { InstrumentPatch { .sample_idx = LONG, .adsr = INFINITE }},
    };
    instruments[0x12] = Instrument {
        .name = "Invalid",
        .keysplit = {
            InstrumentPatch {
                .min_note = 0,
                .sample_idx = PULSE_25,
                .adsr = DEMO,
            },
            InstrumentPatch {
                .min_note = 60,
                .sample_idx = 0x10,
                .adsr = MUSIC_BOX,
            },
            InstrumentPatch {
                .min_note = 48,
                .sample_idx = TRIANGLE,
                .adsr = INFINITE,
            },
        },
    };

    ChipList chips{ChipKind::Spc700};

    Sequence sequence = empty_sequence();

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = move(samples),
        .instruments = move(instruments),
        .chips = move(chips),
        .sequence = move(sequence),
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

    Sequence sequence{{{}, {}, {}, {}, {}, {}, {}, {}}};
    sequence[0][3].settings.n_effect_col = 2;

    // frame 0
    sequence[0][0].blocks.push_back(TrackBlock {
        .begin_tick = at(0),
        .loop_count = 1,
        .pattern = Pattern {
            .length_ticks = at(8),
            .events = {
                // Since ch0 has only 1 effect column,
                // the delay should neither be visible on-screen
                // nor affect the sequencer.
                // TODO write a unit test to make sure the sequencer only uses
                // in-bounds delays.
                Ev(at(0), pitch(5, 7)).instr(0).no_effect().delay(16),
                Ev(at(1), pitch(6, 2)),
                Ev(at(4+0), pitch(5, 7+2)),
                Ev(at(4+1), pitch(6, 2+2)),
            },
        },
    });
    sequence[0][1].blocks.push_back(TrackBlock::from_events(
        at(0), at(8),
        EventList {
            Ev(at(0, 36), NOTE_CUT),
            Ev(at(1, 24), pitch(7, -3)).instr(0),
            Ev(at(2, 0), pitch(7, 6)),
            Ev(at(2, 24), pitch(7, 7)),
            Ev(at(3, 24), pitch(7, 9)),
            Ev(at(4, 24), pitch(7, 4)),
            Ev(at(5, 24), pitch(7, 2)),
            Ev(at(6, 24), pitch(7, 1)),
        }));

    // frame 1
    sequence[0][0].blocks.push_back(TrackBlock::from_events(
        at(8), at(8),
        EventList {
            Ev(at(0), pitch(6, 4)).instr(0),
            Ev(at(1), pitch(6, -1)),
            Ev(at(2), pitch(6, 4)),
            Ev(at(3), pitch(6, 7)),
            Ev(at(4), pitch(6, 6)),
            Ev(at(5), pitch(6, 1)),
            Ev(at(6), pitch(6, -2)),
            Ev(at(7), pitch(6, 1)),
        }));
    sequence[0][1].blocks.push_back(TrackBlock::from_events(
        at(8), at(8),
        EventList {
            Ev(at(0), pitch(6, 7)).instr(0).delay(3),
            Ev(at(1, 24), pitch(7, -1)),
            Ev(at(3), pitch(7, 4)),
            Ev(at(4), pitch(7, -2)).delay(3),
            Ev(at(5, 24), pitch(7, 7)),
            Ev(at(5, 36), pitch(7, 6)),
            Ev(at(6), pitch(7, 4)),
        }));
    sequence[0][2].blocks.push_back(TrackBlock::from_events(
        at(8), at(8),
        EventList {
            Ev(at(0), pitch(7, -1)).instr(0).delay(6),
            Ev(at(4), pitch(7, 1)).delay(6),
        }));
    sequence[0][3].blocks.push_back(TrackBlock::from_events(
        at(8), at(8),
        EventList {
            Ev(at(0), pitch(7, 4)).instr(0).no_effect().delay(9),
            Ev(at(4), pitch(7, 6)).no_effect().delay(9),
        }));

    return DocumentCopy {
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = move(samples),
        .instruments = move(instruments),
        .chips = move(chips),
        .sequence = move(sequence),
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

    Sequence sequence{{{}, {}, {}, {}, {}, {}, {}, {}}};
    for (int i = 0; i < 8; i++) {
        sequence[0][(size_t) i].blocks.push_back(TrackBlock::from_events(
            at(0), at(8),
            EventList {
                Ev(i * (48 / 4), Note(Chromatic(60 + 2 * i))).instr(0),
            }));
    }

    return DocumentCopy {
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = move(samples),
        .instruments = move(instruments),
        .chips = move(chips),
        .sequence = move(sequence),
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

    auto noise_loop = SequenceTrack{TrackBlock::from_events({
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

    auto dpcm_level = SequenceTrack{TrackBlock::from_events({
        Ev(0, {}).volume(0x7f)
    })};

    timeline.push_back([&]() -> TimelineFrame {
        // Add two blocks into one grid cell, as a test case.
        auto ch0 = SequenceTrack{
            TrackBlock{0, BeatOrEnd(8),
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
            TrackBlock{8, BeatOrEnd(16),
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

        auto ch1 = SequenceTrack{
            TrackBlock{0, 4, Pattern{generate_bass(1, 5), 1}},
            TrackBlock{4, 8, Pattern{generate_bass(1, 7), 1}},
            TrackBlock{8, 12, Pattern{generate_bass(1, 5), 1}},
            TrackBlock{12, 16, Pattern{generate_bass(1, 7), 1}},
        };

        auto tri = SequenceTrack{
            TrackBlock{0, 8, {{
                // 0
                Ev(0, pitch(5, 9)),
                Ev(at(3, 2, 4), pitch(5, 7)),
                Ev(at(3, 3, 4), pitch(5, 9)),
                Ev(4, pitch(5, 7)),
                Ev(6, pitch(5, 4)),
            }}},
            TrackBlock{8, 16, {{
                // 8
                Ev(0, pitch(5, 9)),
                Ev(at(3, 2, 4), pitch(5, 7)),
                Ev(at(3, 3, 4), pitch(5, 9)),
                Ev(4, pitch(6, 0)),
                Ev(6, pitch(6, -1)),
            }}},
        };

        return TimelineFrame{
            .nbeats = 16,
            .chip_channel_cells = {{
                move(ch0), move(ch1), move(tri), noise_loop, dpcm_level
            }},
        };
    }());
    timeline.push_back([&]() -> TimelineFrame {
        auto ch0 = SequenceTrack{TrackBlock::from_events({
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
        auto ch1 = SequenceTrack{TrackBlock::from_events(generate_bass(8))};

        auto tri = SequenceTrack{
            TrackBlock{0, 8, {{
                // 0
                Ev(0, pitch(5, 9)),
                Ev(at(3, 2, 4), pitch(5, 7)),
                Ev(at(3, 3, 4), pitch(5, 9)),
                Ev(4, pitch(5, 7)),
                Ev(6, pitch(5, 4)),
            }}},
        };

        return TimelineFrame {
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

    timeline.push_back([]() -> TimelineFrame {
        SequenceTrack ch0{TrackBlock::from_events([&] {
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

        SequenceTrack ch1{TrackBlock::from_events({
            Ev(2, {NOTE_CUT}),
            Ev(4, {NOTE_RELEASE}),
        })};

        return TimelineFrame{
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
        return SequenceTrack{TrackBlock::from_events({
            Ev(0, note).instr(0)
        })};
    };

    Timeline timeline;

    timeline.push_back([&]() -> TimelineFrame {
        SequenceTrack ch0{};

        SequenceTrack ch1{TrackBlock::from_events({
            // Events go here.
        })};

        SequenceTrack ch2{TrackBlock{0, BeatOrEnd(8),
            Pattern{EventList{
                // Events go here.
            }}
        }};

        SequenceTrack ch3{TrackBlock{0, END_OF_GRID,
            Pattern{EventList{
                // Events go here.
            }, 4}
        }};

        return TimelineFrame{
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

#endif

/// Document used to test block rendering and editing, as well as cursor movement.
static Document block_test() {
    SequencerOptions sequencer_options{.target_tempo = 150, .ticks_per_beat = 48};

    Samples samples;
    samples[0] = triangle();

    Instruments instruments;
    instruments[0] = music_box(0);

    ChipList chips{ChipKind::Spc700};

    constexpr TickT BEAT_LEN = 48;

    // Length 96.
    auto unlooped2 = [](int start_beat) -> TrackBlock {
        return TrackBlock {
            .begin_tick = start_beat * BEAT_LEN,
            .loop_count = 1,
            .pattern = Pattern {
                .length_ticks = 2 * BEAT_LEN,
                .events = {},
            },
        };
    };
    // Length 96.
    auto looped2 = [](int start_beat) -> TrackBlock {
        return TrackBlock {
            .begin_tick = start_beat * BEAT_LEN,
            .loop_count = 2,
            .pattern = Pattern {
                .length_ticks = BEAT_LEN,
                .events = {},
            },
        };
    };

    // Measure boundaries lie at multiples of 192 ticks.
    std::vector<TrackBlock> ch0;
    ch0.push_back(unlooped2(2));
    ch0.push_back(unlooped2(4));

    std::vector<TrackBlock> ch1;
    ch1.push_back(looped2(2));
    ch1.push_back(looped2(4));

    Sequence sequence{{
        SequenceTrack(std::move(ch0)),
        SequenceTrack(std::move(ch1)),
        {},
        {},
        {},
        {},
        {},
        {},
    }};
    assert(sequence[0].size() == 8);

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = move(samples),
        .instruments = move(instruments),
        .chips = move(chips),
        .sequence = move(sequence),
    };
}

std::map<std::string, doc::Document> const DOCUMENTS = [] {
    std::map<std::string, doc::Document> out;
    out.insert({"instruments", instruments()});
    out.insert({"dream-fragments", dream_fragments()});
    out.insert({"all-channels", all_channels()});
    // out.insert({"world-revolution", world_revolution()});
    // out.insert({"render-test", render_test()});
    // out.insert({"audio-test", audio_test()});
     out.insert({"block-test", block_test()});
    return out;
}();

}
