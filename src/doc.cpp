#include "doc.h"

#include "chip_kinds.h"
#include "util/release_assert.h"

#include <cmath>

namespace doc {

static TimeInPattern at(BeatFraction anchor_beat) {
    return TimeInPattern{anchor_beat, 0};
}

static TimeInPattern at_frac(int start, int num, int den) {
    return TimeInPattern{start + BeatFraction(num, den), 0};
}

static Note pitch(int octave, int semitone) {
    return Note{static_cast<ChromaticInt>(12 * octave + semitone)};
}

Document dummy_document() {
    // Excerpt from "Chrono Cross - Dream Fragments".
    // This tests the ability to nudge notes both early and later,
    // and even before the beginning of a pattern.

    // Global options
    SequencerOptions sequencer_options{
        .ticks_per_beat = 43,
    };

    // We only have 1 chip.
    auto const chip_kind = chip_kinds::ChipKind::Apu1;
    using ChannelID = chip_kinds::Apu1ChannelID;
    Document::ChipList chips{chip_kind};

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
                {at_frac(0, 3, 4), {NOTE_CUT}},
                {at_frac(1, 1, 2), {.note=pitch(7, -3), .instr=0}},
                {at_frac(2, 0, 2), {pitch(7, 6)}},
                {at_frac(2, 1, 2), {pitch(7, 7)}},
                {at_frac(3, 1, 2), {pitch(7, 9)}},
                {at_frac(4, 1, 2), {pitch(7, 4)}},
                {at_frac(5, 1, 2), {pitch(7, 2)}},
                {at_frac(6, 1, 2), {pitch(7, 1)}},
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
                {at_frac(1, 1, 2), {pitch(7, -1)}},
                {at(3), {pitch(7, 4)}},
                {{4, 4}, {pitch(7, 6)}},
                {at_frac(5, 2, 4), {pitch(7, 7)}},
                {at_frac(5, 3, 4), {pitch(7, 6)}},
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

    return DocumentCopy {
        .chips = chips,
        .sequence = sequence,
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .instruments = {instrument}
    };
}

}
