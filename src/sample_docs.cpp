#include "sample_docs.h"

#include "doc.h"
#include "chip_kinds.h"
#include "edit_util/shorthand.h"
#include "util/release_assert.h"

#include <cmath>

namespace sample_docs {

using namespace doc;
using namespace edit_util::shorthand;

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
    using ChannelID = chip_kinds::Apu1ChannelID;
    ChipList chips{chip_kind};

    Sequence sequence;

    // seq ind 0
    sequence.push_back([] {
        ChipChannelTo<EventList> chip_channel_events;

        chip_channel_events.push_back({
            {
                // TimeInPattern, RowEvent
                {at(0), {.note=pitch(5, 7), .instr=0}},
                {at(1), {pitch(6, 2)}},
                {at(4+0), {pitch(5, 7+2)}},
                {at(4+1), {pitch(6, 2+2)}},
            },
            {
                {at(0, 3, 4), {NOTE_CUT}},
                {at(1, 1, 2), {.note=pitch(7, -3), .instr=0}},
                {at(2, 0, 2), {pitch(7, 6)}},
                {at(2, 1, 2), {pitch(7, 7)}},
                {at(3, 1, 2), {pitch(7, 9)}},
                {at(4, 1, 2), {pitch(7, 4)}},
                {at(5, 1, 2), {pitch(7, 2)}},
                {at(6, 1, 2), {pitch(7, 1)}},
            },
        });
        release_assert(chip_channel_events[0].size() == (int)ChannelID::COUNT);

        return SequenceEntry {
            .nbeats = 8,
            .chip_channel_events = chip_channel_events,
        };
    }());

    // seq ind 1
    sequence.push_back([] {
        ChipChannelTo<EventList> chip_channel_events;

        chip_channel_events.push_back({
            {
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
            },
            {
                {{0, 4}, {pitch(7, 4)}},
                {at(1, 1, 2), {pitch(7, -1)}},
                {at(3), {pitch(7, 4)}},
                {{4, 4}, {pitch(7, 6)}},
                {at(5, 2, 4), {pitch(7, 7)}},
                {at(5, 3, 4), {pitch(7, 6)}},
                {at(6), {pitch(7, 4)}},
                {{8, 3}, {pitch(6, 2)}},
            },
        });
        release_assert(chip_channel_events[0].size() == (int)ChannelID::COUNT);

        return SequenceEntry {
            .nbeats = 8,
            .chip_channel_events = chip_channel_events,
        };
    }());

    // TODO add method to validate sequence invariants

    auto instrument = [] {
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
    }();

    Instruments instruments;
    instruments[0] = instrument;

    return DocumentCopy {
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = std::move(instruments),
        .chips = chips,
        .sequence = sequence,
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

    Sequence sequence;

    auto generate_bass = [&](int nbeats) -> EventList {
        EventList out;
        for (int beat = 0; beat < nbeats; beat++) {
            int note = (beat / 4 % 2 == 0)
                ? pitch(0, 5).value
                : pitch(0, 7).value;
            out.push_back({at(beat), {pitch(3, note)}});
            out.push_back({at(beat, 1, 4), {pitch(3, note + 7)}});
            out.push_back({at(beat, 2, 4), {pitch(3, note + 12)}});
            out.push_back({at(beat, 3, 4), {pitch(3, note + 7)}});
        }

        out[0].v.instr = BASS;
        return out;
    };

    sequence.push_back([&] {
        ChipChannelTo<EventList> chip_channel_events;
        chip_channel_events.push_back({
            EventList{
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
                // 8
                {at(8), {pitch(6, 0)}},
                {at(8, 4, 8), {pitch(6, -1)}},
                {at(8, 6, 8), {pitch(6, 0)}},
                {at(8, 7, 8), {pitch(6, -1)}},
                {at(9), {pitch(5, 9)}},
                {at(9, 1, 2), {pitch(5, 7)}},
                {at(10), {pitch(5, 9)}},
                {at(10, 1, 2), {pitch(6, -1)}},
                {at(11), {pitch(6, 0)}},
                {at(11, 1, 2), {pitch(6, 2)}},
                // 12
                {at(12), {pitch(6, 4)}},
                {at(14), {pitch(6, 2)}},
                {at(15), {pitch(5, 9)}},
                {at(15, 1, 2), {pitch(6, -1)}},
            },
            generate_bass(16),
        });

        return SequenceEntry {
            .nbeats = 16,
            .chip_channel_events = chip_channel_events,
        };
    }());

    sequence.push_back([&] {
        ChipChannelTo<EventList> chip_channel_events;
        chip_channel_events.push_back({
            EventList{
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
            },
            generate_bass(8),
        });

        return SequenceEntry {
            .nbeats = 8,
            .chip_channel_events = chip_channel_events,
        };
    }());

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = instruments,
        .chips = chips,
        .sequence = sequence,
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

    Sequence sequence;

    sequence.push_back([&] {
        ChipChannelTo<EventList> chip_channel_events;

        EventList ch1;
        for (int i = 0; i <= 10; i++) {
            // Play MIDI pitches 0, 12... 120.
            ch1.push_back({at_delay(i, 0, 2, 4 * (i - 5)), {pitch(i, 0)}});
            ch1.push_back({at_delay(i, 1, 2, 4 * (i - 5)), {i % 2 == 0 ? NOTE_CUT : NOTE_RELEASE}});
        }
        ch1[0].v.instr = 2;

        EventList ch2;
        ch2.push_back({at(2), {NOTE_CUT}});
        ch2.push_back({at(4), {NOTE_RELEASE}});

        chip_channel_events.push_back({ch1, ch2});

        return SequenceEntry {
            .nbeats = 10 + 1,
            .chip_channel_events = chip_channel_events,
        };
    }());

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = instruments,
        .chips = chips,
        .sequence = sequence,
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

    Sequence sequence;

    sequence.push_back([&] {
        ChipChannelTo<EventList> chip_channel_events;

        // first apu1
        {
            EventList ch1;
            ch1.push_back({at(0), RowEvent{pitch(5, 0), 0}});

            EventList ch2;
            ch2.push_back({at(0), RowEvent{pitch(5, 4), 0}});

            chip_channel_events.push_back({std::move(ch1), std::move(ch2)});
        }
        // second apu1
        {
            EventList ch1;
            ch1.push_back({at(0), RowEvent{pitch(5, 8), 0}});

            EventList ch2;
            ch2.push_back({at(0), RowEvent{pitch(5, 12), 0}});

            chip_channel_events.push_back({std::move(ch1), std::move(ch2)});
        }

        return SequenceEntry {
            .nbeats = 4,
            .chip_channel_events = chip_channel_events,
        };
    }());

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = instruments,
        .chips = chips,
        .sequence = sequence,
    };
}

/// Test song, plays four notes from two chips nonstop.
/// Check for audio underruns (crackling) by recording and viewing in a spectrogram.
static Document empty() {
    SequencerOptions sequencer_options{.ticks_per_beat = 24};

    auto instrument = [] {
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
    }();

    Instruments instruments;
    instruments[0] = instrument;

    ChipList chips{ChipKind::Apu1};

    Sequence sequence;

    sequence.push_back(SequenceEntry {
        .nbeats = 16,
        .chip_channel_events = {
            // chip 0
            {
                // channel 0
                {},
                // channel 1
                {},
            }
        },
    });

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = instruments,
        .chips = chips,
        .sequence = sequence,
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
    return out;
}();

}
