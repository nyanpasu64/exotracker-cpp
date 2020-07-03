#pragma once

#include "doc.h"
#include "util/copy_move.h"

#include <cstdint>

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


struct [[nodiscard]] MaybeSequencerTime {
private:
    SequencerTime _timestamp;

public:
    // Clone API at https://en.cppreference.com/w/cpp/utility/optional#Member_functions

    constexpr MaybeSequencerTime(SequencerTime timestamp)
        : _timestamp{timestamp}
    {}

    static constexpr MaybeSequencerTime none() {
        return SequencerTime{(uint16_t) -1, (uint16_t) -1, -1, -1};
    }

    constexpr MaybeSequencerTime()
        : MaybeSequencerTime{none()}
    {}

    constexpr MaybeSequencerTime(std::nullopt_t)
        : MaybeSequencerTime{}
    {}

    CONSTEXPR_COPY(MaybeSequencerTime)
    // std::optional also has constexpr move, but what does that mean?
    DEFAULT_MOVE(MaybeSequencerTime)

    constexpr bool has_value() const {
        return *this != none();
    }

    explicit operator bool() const {
        return has_value();
    }

    SequencerTime const & value() const {
        return _timestamp;
    }

    SequencerTime & value() {
        return _timestamp;
    }

    SequencerTime const & operator*() const {
        return _timestamp;
    }

    SequencerTime & operator*() {
        return _timestamp;
    }

    SequencerTime const * operator->() const {
        return &_timestamp;
    }

    SequencerTime * operator->() {
        return &_timestamp;
    }

    EQUALABLE_SIMPLE(MaybeSequencerTime, _timestamp)
};
static_assert(
    sizeof(MaybeSequencerTime) <= 8, "MaybeSequencerTime over 8 bytes, not atomic"
);


}
