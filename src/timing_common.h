#pragma once

#include "doc.h"

#include <cstdint>
#include <optional>

namespace timing {

struct PatternAndBeat {
    doc::SeqEntryIndex seq_entry_index = 0;
    doc::BeatFraction beat = 0;
};

// Atomically written by audio thread, atomically read by GUI.
// Make sure this fits within 8 bytes.
struct [[nodiscard]] alignas(uint64_t) SequencerTime {
    uint16_t seq_entry_index;

    // should this be removed, or should the audio thread keep track of this
    // for the GUI thread rendering?
    uint16_t curr_ticks_per_beat;

    // sequencer.h BeatPlusTick is signed.
    // Neither beats nor ticks should be negative in regular playback,
    // but mark as signed just in case.
    int16_t beats;
    int16_t ticks;

    constexpr SequencerTime(
        uint16_t seq_entry_index,
        uint16_t curr_ticks_per_beat,
        int16_t beats,
        int16_t ticks
    )
        : seq_entry_index{seq_entry_index}
        , curr_ticks_per_beat{curr_ticks_per_beat}
        , beats{beats}
        , ticks{ticks}
    {}

    constexpr SequencerTime() : SequencerTime{0, 1, 0, 0} {}

    CONSTEXPR_COPY(SequencerTime)
    EQUALABLE(SequencerTime, (seq_entry_index, curr_ticks_per_beat, beats, ticks))
};
static_assert(sizeof(SequencerTime) <= 8, "SequencerTime over 8 bytes, not atomic");

using MaybeSequencerTime = std::optional<SequencerTime>;

}
