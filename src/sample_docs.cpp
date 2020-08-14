#include "sample_docs.h"

#include "doc.h"
#include "chip_kinds.h"
#include "doc_util/shorthand.h"
#include "util/release_assert.h"

#include <cmath>

namespace sample_docs {

using namespace doc;
using namespace doc_util::shorthand;

Instrument music_box() {
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

    DenseMap<GridIndex, GridCell> grid_cells {
        {.nbeats = 8},
        {.nbeats = 8},
    };

    Timeline channel_0;
    channel_0.push_back({TimelineBlock{0, END_OF_GRID,
        Pattern{{
            // TimeInPattern, RowEvent
            {at(0), {.note=pitch(5, 7), .instr=0}},
            {at(1), {pitch(6, 2)}},
            {at(4+0), {pitch(5, 7+2)}},
            {at(4+1), {pitch(6, 2+2)}},
        }},
    }});
    channel_0.push_back({TimelineBlock{0, END_OF_GRID,
        Pattern{{
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
        }},
    }});

    Timeline channel_1;
    channel_1.push_back({TimelineBlock{0, END_OF_GRID,
        Pattern{{
            {at(0, 3, 4), {NOTE_CUT}},
            {at(1, 1, 2), {.note=pitch(7, -3), .instr=0}},
            {at(2, 0, 2), {pitch(7, 6)}},
            {at(2, 1, 2), {pitch(7, 7)}},
            {at(3, 1, 2), {pitch(7, 9)}},
            {at(4, 1, 2), {pitch(7, 4)}},
            {at(5, 1, 2), {pitch(7, 2)}},
            {at(6, 1, 2), {pitch(7, 1)}},
        }},
    }});
    channel_1.push_back({TimelineBlock{0, END_OF_GRID,
        Pattern{{
            {{0, 4}, {pitch(7, 4)}},
            {at(1, 1, 2), {pitch(7, -1)}},
            {at(3), {pitch(7, 4)}},
            {{4, 4}, {pitch(7, 6)}},
            {at(5, 2, 4), {pitch(7, 7)}},
            {at(5, 3, 4), {pitch(7, 6)}},
            {at(6), {pitch(7, 4)}},
        }},
    }});

    ChipChannelTo<Timeline> chip_channel_timelines{
        // chip 0
        {channel_0, channel_1}
    };
    // TODO add method to validate ChipChannelTo invariants
    //  and make sure all timelines are same length as grid_cells

    Instruments instruments;
    instruments[0] = music_box();

    return DocumentCopy {
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = std::move(instruments),
        .chips = chips,
        .grid_cells = grid_cells,
        // too lazy for std::move
        .chip_channel_timelines = chip_channel_timelines,
    };
}

/// Excerpt from "Chrono Trigger - World Revolution".
/// This tests multiple grid cells and blocks (patterns) of uneven lengths,
/// and pattern looping.
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

    DenseMap<GridIndex, GridCell> grid_cells {
        {.nbeats = 16},
        {.nbeats = 8},
    };

    Timeline channel_0;

    // grid 0
    channel_0.push_back({
        // Add two blocks into one grid cell, as a test case.
        TimelineBlock{0, BeatOrEnd(8),
            Pattern{EventList{
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
                {at(6), {pitch(5, 7)}},
                {at(7), {pitch(5, 9)}},
                {at(7, 1, 2), {pitch(6, -1)}},
            }}
        },
        TimelineBlock{8, BeatOrEnd(16),
            Pattern{EventList{
                // 0
                {at(0), {pitch(6, 0)}},
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
        }
    });
    // grid 1
    channel_0.push_back({TimelineBlock{0, END_OF_GRID,
        Pattern{EventList{
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
        }},
    }});

    Timeline channel_1;

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
        return out;
    };

    // grid 0
    channel_1.push_back({
        TimelineBlock{0, 4, Pattern{generate_bass(1, 5), 1}},
        TimelineBlock{4, 8, Pattern{generate_bass(1, 7), 1}},
        TimelineBlock{8, 12, Pattern{generate_bass(1, 5), 1}},
        TimelineBlock{12, 16, Pattern{generate_bass(1, 7), 1}},
    });
    // grid 1
    channel_1.push_back({TimelineBlock{0, END_OF_GRID, Pattern{generate_bass(8), 8}}});

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = instruments,
        .chips = chips,
        .grid_cells = grid_cells,
        // too lazy for std::move
        .chip_channel_timelines = {{channel_0, channel_1}},
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

    DenseMap<GridIndex, GridCell> grid_cells {
        {.nbeats = 10 + 1}
    };

    ChipList chips{ChipKind::Apu1};

    Timeline channel_0{{
        TimelineBlock{0, END_OF_GRID,
            Pattern{[&] {
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
            }()}
        }
    }};

    Timeline channel_1{{
        TimelineBlock{0, END_OF_GRID,
            Pattern{[&] {
                EventList ev;
                ev.push_back({at(2), {NOTE_CUT}});
                ev.push_back({at(4), {NOTE_RELEASE}});
                return ev;
            }()}
        }
    }};

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = instruments,
        .chips = chips,
        .grid_cells = grid_cells,
        // too lazy for std::move
        .chip_channel_timelines = {{channel_0, channel_1}},
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

    DenseMap<GridIndex, GridCell> grid_cells {
        {.nbeats = 4},
    };

    ChipList chips{ChipKind::Apu1, ChipKind::Apu1};

    auto get_channel = [&] (Note note) {
        return Timeline{{TimelineBlock{0, END_OF_GRID,
            Pattern{EventList{
                {at(0), RowEvent{note, 0}}
            }}
        }}};
    };

    Timeline channel_0 = get_channel(pitch(5, 0));
    Timeline channel_1 = get_channel(pitch(5, 4));
    Timeline channel_2 = get_channel(pitch(5, 8));
    Timeline channel_3 = get_channel(pitch(5, 12));

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = instruments,
        .chips = chips,
        .grid_cells = grid_cells,
        // too lazy for std::move
        .chip_channel_timelines = {{channel_0, channel_1}, {channel_2, channel_3}},
    };
}

/// Empty document with one grid cell.
/// Channel 0 has a block/pattern without events, and Channel 1 has no pattern.
static Document empty() {
    SequencerOptions sequencer_options{.ticks_per_beat = 24};

    Instruments instruments;
    instruments[0] = music_box();

    ChipList chips{ChipKind::Apu1};

    DenseMap<GridIndex, GridCell> grid_cells {
        {.nbeats = 16},
    };

    // One empty pattern.
    Timeline channel_0;
    channel_0.push_back({TimelineBlock{0, END_OF_GRID,
        Pattern{{}}
    }});

    // No patterns at all.
    Timeline channel_1;
    channel_1.push_back({});

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = instruments,
        .chips = chips,
        .grid_cells = grid_cells,
        // too lazy for std::move
        .chip_channel_timelines = {{channel_0, channel_1}},
    };
}

/// Document used to test block rendering and cursor movement,
/// and see how blocks look when overlapping
static Document block_test() {
    SequencerOptions sequencer_options{.ticks_per_beat = 24};

    Instruments instruments;
    instruments[0] = music_box();

    ChipList chips{ChipKind::Apu1, ChipKind::Apu1};

    DenseMap<GridIndex, GridCell> grid_cells {
        {.nbeats = 2},
        {.nbeats = 2},
        {.nbeats = 2},
        {.nbeats = 2},
        {.nbeats = 2},
    };

    TimelineBlock unlooped{0, END_OF_GRID, Pattern{{}}};
    TimelineBlock looped{0, END_OF_GRID, Pattern{{}, 1}};

    Timeline channel_0;
    channel_0.push_back({unlooped});
    channel_0.push_back({});
    channel_0.push_back({looped});
    channel_0.push_back({});
    channel_0.push_back({unlooped});

    Timeline channel_1;
    channel_1.push_back({});
    channel_1.push_back({looped});
    channel_1.push_back({looped});
    channel_1.push_back({unlooped});
    channel_1.push_back({});

    Timeline empty;
    for (int i = 0; i < 5; i++) empty.push_back({});

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = instruments,
        .chips = chips,
        .grid_cells = grid_cells,
        // too lazy for std::move
        .chip_channel_timelines = {{channel_0, channel_1}, {empty, empty}},
    };
}


std::string const DEFAULT_DOC = "world-revolution";

std::map<std::string, doc::Document> const DOCUMENTS = [] {
    std::map<std::string, doc::Document> out;
    out.insert({"dream-fragments", dream_fragments()});
    out.insert({"world-revolution", world_revolution()});
    out.insert({"render-test", render_test()});
    out.insert({"audio-test", audio_test()});
    out.insert({"empty", empty()});
    out.insert({"block-test", block_test()});
    return out;
}();

}
