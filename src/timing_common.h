#pragma once

#include "util/bit.h"  // bit_cast

#include <cstdint>

namespace timing {

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

    uint64_t _flatten() const {
        return bit_cast<uint64_t>(*this);
    }

    [[nodiscard]] bool operator==(SequencerTime const & other) const {
        return this->_flatten() == other._flatten();
    }
    [[nodiscard]] bool operator!=(SequencerTime const & other) const {
        return this->_flatten() != other._flatten();
    }

    constexpr static SequencerTime _none() {
        auto minus_one = uint16_t(-1);
        return SequencerTime{minus_one, minus_one, -1, -1};
    }
};

struct [[nodiscard]] MaybeSequencerTime {
private:
    SequencerTime _timestamp;

public:
    explicit constexpr MaybeSequencerTime(SequencerTime timestamp)
        : _timestamp{timestamp}
    {}

    static constexpr MaybeSequencerTime none() {
        return MaybeSequencerTime{SequencerTime::_none()};
    }

    bool has_value() {
        return _timestamp != SequencerTime::_none();
    }

    SequencerTime get() {
        return _timestamp;
    }

    [[nodiscard]] bool operator==(MaybeSequencerTime const & other) const {
        return _timestamp == other._timestamp;
    }
    [[nodiscard]] bool operator!=(MaybeSequencerTime const & other) const {
        return _timestamp != other._timestamp;
    }
};

}
