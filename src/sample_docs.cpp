#include "sample_docs.h"

#include "doc.h"
#include "chip_kinds.h"
#include "doc_util/shorthand.h"
#include "util/release_assert.h"

#include <cmath>

namespace sample_docs {

using namespace doc;
using namespace doc_util::shorthand;
using std::move;
using std::nullopt;

static Instrument music_box() {
    auto volume = [] {
        std::vector<ByteEnvelope::IntT> volume;
        for (int i = 0; ; i++) {
            volume.push_back(ByteEnvelope::IntT(15 * pow(0.5, i / 20.0)));
            if (volume.back() == 0) {
                break;
            }
        }
        return volume;
    };

    return Instrument{
        .volume={volume()}, .pitch={{}}, .arpeggio={{7, 0}}, .wave_index={{2, 2}}
    };
}

/// Empty document with one grid cell.
/// Channel 0 has a block/pattern without events, and Channel 1 has no pattern.
///
/// Use as a template for porting other documents.
static Document empty() {
    SequencerOptions sequencer_options{.ticks_per_beat = 24};

    Instruments instruments;
    instruments[0] = music_box();

    ChipList chips{ChipKind::Apu1, ChipKind::Apu1};

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
            .chip_channel_cells = {{move(ch0), move(ch1)}, {move(ch2), move(ch3)}},
        };
    }());

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = move(instruments),
        .chips = move(chips),
        .timeline = move(timeline),
    };
}

/// Excerpt from "Chrono Cross - Dream Fragments".
/// This tests the ability to nudge notes both early and later,
/// and even before the beginning of a pattern.
static Document dream_fragments() {
    // Global options
    SequencerOptions sequencer_options{
        .ticks_per_beat = 43,
    };

    // We only have 1 chip.
    auto const chip_kind = chip_kinds::ChipKind::Apu1;
    ChipList chips{chip_kind};

    Timeline timeline;

    timeline.push_back([]() -> TimelineRow {
        TimelineCell ch0{TimelineBlock::from_events({
            // TimeInPattern, RowEvent
            {at(0), {.note=pitch(5, 7), .instr=0}},
            {at(1), {pitch(6, 2)}},
            {at(4+0), {pitch(5, 7+2)}},
            {at(4+1), {pitch(6, 2+2)}},
        })};
        TimelineCell ch1{TimelineBlock::from_events({
            {at(0, 3, 4), {NOTE_CUT}},
            {at(1, 1, 2), {.note=pitch(7, -3), .instr=0}},
            {at(2, 0, 2), {pitch(7, 6)}},
            {at(2, 1, 2), {pitch(7, 7)}},
            {at(3, 1, 2), {pitch(7, 9)}},
            {at(4, 1, 2), {pitch(7, 4)}},
            {at(5, 1, 2), {pitch(7, 2)}},
            {at(6, 1, 2), {pitch(7, 1)}},
        })};
        return TimelineRow{
            .nbeats = 8,
            .chip_channel_cells = {{move(ch0), move(ch1)}},
        };
    }());

    timeline.push_back([] {
        auto ch0 = TimelineCell{TimelineBlock::from_events({
            // TimeInPattern, RowEvent
            {{0, -5}, {pitch(6, 4)}},
            {{0, -2}, {pitch(6, 7)}},
            {{0, 1}, {pitch(7, -1)}},
            {at(1), {pitch(6, -1)}},
            {at(2), {pitch(6, 4)}},
            {at(3), {pitch(6, 7)}},
            {{4, -5}, {pitch(6, 6)}},
            {{4, -2}, {pitch(7, -2)}},
            {{4, 1}, {pitch(7, 1)}},
            {at(5), {pitch(6, 1)}},
            {at(6), {pitch(6, -2)}},
            {at(7), {pitch(6, 1)}},
        })};
        auto ch1 = TimelineCell{TimelineBlock::from_events({
            {{0, 4}, {pitch(7, 4)}},
            {at(1, 1, 2), {pitch(7, -1)}},
            {at(3), {pitch(7, 4)}},
            {{4, 4}, {pitch(7, 6)}},
            {at(5, 2, 4), {pitch(7, 7)}},
            {at(5, 3, 4), {pitch(7, 6)}},
            {at(6), {pitch(7, 4)}},
        })};
        return TimelineRow{
            .nbeats = 8,
            .chip_channel_cells = {{move(ch0), move(ch1)}},
    };
    }());

    Instruments instruments;
    instruments[0] = music_box();

    return DocumentCopy {
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = move(instruments),
        .chips = move(chips),
        .timeline = move(timeline),
    };
}

/// Excerpt from "Chrono Trigger - World Revolution".
/// This tests multiple sequence entries (patterns) of uneven lengths.
static Document world_revolution() {
    SequencerOptions sequencer_options{.ticks_per_beat = 23};

    Instruments instruments;

    InstrumentIndex BASS = 0;
    instruments[BASS] = Instrument {
        .volume = {{7, 7, 7, 7, 7, 3}},
        .pitch = {{}},
        .arpeggio = {{}},
        .wave_index = {{1, 1, 1, 0}},
    };

    InstrumentIndex TRUMPET = 1;
    instruments[TRUMPET] = Instrument {
        .volume = {{5, 6, 7, 8, 8, 9}},
        .pitch = {{}},
        .arpeggio = {{}},
        .wave_index = {{1, 1, 0}},
    };

    ChipList chips{ChipKind::Apu1};

    Timeline timeline;

    auto generate_bass = [&](int nbeats, int offset = 5) -> EventList {
        EventList out;
        for (int beat = 0; beat < nbeats; beat++) {
            int note = (beat / 4 % 2 == 0)
                ? pitch(0, offset).value
                : pitch(0, offset + 2).value;
            out.push_back({at(beat), {pitch(3, note)}});
            out.push_back({at(beat, 1, 4), {pitch(3, note + 7)}});
            out.push_back({at(beat, 2, 4), {pitch(3, note + 12)}});
            out.push_back({at(beat, 3, 4), {pitch(3, note + 7)}});
        }

        out[0].v.instr = BASS;

        out[0].v.effects[0] = Effect("AA", 0);
        out[1].v.effects[0] = Effect("II", 0);
        out[2].v.effects[0] = Effect("AI", 0);
        out[3].v.effects[0] = Effect("IA", 0);

        return out;
    };

    timeline.push_back([&]() -> TimelineRow {
        // Add two blocks into one grid cell, as a test case.
        auto ch0 = TimelineCell{
            TimelineBlock{0, BeatOrEnd(8),
                Pattern{EventList{
                    // TimeInPattern, RowEvent
                    // 0
                    {at(0), {pitch(6, 0), TRUMPET, 0xf, {Effect("0A", 0)}}},
                    {
                        at(0, 4, 8),
                        {pitch(6, -1), nullopt, nullopt, {Effect("0Q", 0)}}
                    },
                    {at(0, 6, 8), {pitch(6, 0)}},
                    {at(0, 7, 8), {pitch(6, -1)}},
                    {at(1), {pitch(5, 9)}},
                    {at(1, 1, 2), {pitch(5, 7)}},
                    {at(2), {pitch(5, 9)}},
                    {at(2, 1, 2), {pitch(5, 2)}},
                    {at(3), {pitch(5, 4)}},
                    {at(3, 1, 2), {pitch(5, 7)}},
                    // 4
                    {at(4), {pitch(5, 9)}},
                    {at(6), {pitch(5, 7)}},
                    {at(7), {pitch(5, 9)}},
                    {at(7, 1, 2), {pitch(6, -1)}},
                }}
            },
            TimelineBlock{8, BeatOrEnd(16),
                Pattern{EventList{
                    // 0
                    {at(0), {pitch(6, 0), nullopt, 0xf}},
                    {at(0, 4, 8), {pitch(6, -1)}},
                    {at(0, 6, 8), {pitch(6, 0)}},
                    {at(0, 7, 8), {pitch(6, -1)}},
                    {at(1), {pitch(5, 9)}},
                    {at(1, 1, 2), {pitch(5, 7)}},
                    {at(2), {pitch(5, 9)}},
                    {at(2, 1, 2), {pitch(6, -1)}},
                    {at(3), {pitch(6, 0)}},
                    {at(3, 1, 2), {pitch(6, 2)}},
                    // 4
                    {at(4), {pitch(6, 4)}},
                    {at(6), {pitch(6, 2)}},
                    {at(7), {pitch(5, 9)}},
                    {at(7, 1, 2), {pitch(6, -1)}},
                }},
            },
        };

        auto ch1 = TimelineCell{
            TimelineBlock{0, 4, Pattern{generate_bass(1, 5), 1}},
            TimelineBlock{4, 8, Pattern{generate_bass(1, 7), 1}},
            TimelineBlock{8, 12, Pattern{generate_bass(1, 5), 1}},
            TimelineBlock{12, 16, Pattern{generate_bass(1, 7), 1}},
        };

        return TimelineRow{
            .nbeats = 16,
            .chip_channel_cells = {{move(ch0), move(ch1)}},
        };
    }());
    timeline.push_back([&]() -> TimelineRow {
        auto ch0 = TimelineCell{TimelineBlock::from_events({
            // TimeInPattern, RowEvent
            // 0
            {at(0), {pitch(6, 0), TRUMPET}},
            {at(0, 4, 8), {pitch(6, -1)}},
            {at(0, 6, 8), {pitch(6, 0)}},
            {at(0, 7, 8), {pitch(6, -1)}},
            {at(1), {pitch(5, 9)}},
            {at(1, 1, 2), {pitch(5, 7)}},
            {at(2), {pitch(5, 9)}},
            {at(2, 1, 2), {pitch(5, 2)}},
            {at(3), {pitch(5, 4)}},
            {at(3, 1, 2), {pitch(5, 7)}},
            // 4
            {at(4), {pitch(5, 9)}},
            {at(5), {pitch(6, -1)}},
            {at(6), {pitch(6, 0)}},
            {at(7), {pitch(6, 2)}},
        })};
        auto ch1 = TimelineCell{TimelineBlock::from_events(generate_bass(8))};

        return TimelineRow {
            .nbeats = 8,
            .chip_channel_cells = {{move(ch0), move(ch1)}},
    };
    }());

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = move(instruments),
        .chips = move(chips),
        .timeline = move(timeline),
    };
}

/// Test song, populated with note cuts, releases, and stuff.
static Document render_test() {
    SequencerOptions sequencer_options{.ticks_per_beat = 24};

    Instruments instruments;

    for (size_t i = 0; i <= 2; i++) {
        instruments[i] = Instrument {
            .volume = {{15}},
            .pitch = {{}},
            .arpeggio = {{}},
            .wave_index = {{int8_t(i)}},
        };
    }

    ChipList chips{ChipKind::Apu1};

    Timeline timeline;

    timeline.push_back([]() -> TimelineRow {
        TimelineCell ch0{TimelineBlock::from_events([&] {
            EventList ev;
            for (int i = 0; i <= 10; i++) {
                // Play MIDI pitches 0, 12... 120.
                ev.push_back({at_delay(i, 0, 2, 4 * (i - 5)), {pitch(i, 0)}});
                ev.push_back({
                    at_delay(i, 1, 2, 4 * (i - 5)),
                    {i % 2 == 0 ? NOTE_CUT : NOTE_RELEASE}
                });
            }
            ev[0].v.instr = 2;
            return ev;
        }())};

        TimelineCell ch1{TimelineBlock::from_events({
            {at(2), {NOTE_CUT}},
            {at(4), {NOTE_RELEASE}},
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
        .timeline = move(timeline),
    };
}

/// Test song, plays four notes from two chips nonstop.
/// Check for audio underruns (crackling) by recording and viewing in a spectrogram.
static Document audio_test() {
    SequencerOptions sequencer_options{.ticks_per_beat = 24};

    Instruments instruments;
    instruments[0] = Instrument {
        .volume = {{15}},
        .pitch = {{}},
        .arpeggio = {{}},
        .wave_index = {{2}},
    };

    ChipList chips{ChipKind::Apu1, ChipKind::Apu1};

    auto get_channel = [&] (Note note) {
        return TimelineCell{TimelineBlock::from_events({
            {at(0), RowEvent{note, 0}}
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
        .timeline = move(timeline),
    };
}


std::string const DEFAULT_DOC = "world-revolution";

std::map<std::string, doc::Document> const DOCUMENTS = [] {
    std::map<std::string, doc::Document> out;
    out.insert({"empty", empty()});
    out.insert({"dream-fragments", dream_fragments()});
    out.insert({"world-revolution", world_revolution()});
    out.insert({"render-test", render_test()});
    out.insert({"audio-test", audio_test()});
    out.insert({"block-test", block_test()});
    return out;
}();

}
