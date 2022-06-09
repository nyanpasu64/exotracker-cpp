#pragma once

#include "doc.h"
#include "util/compare.h"
#include "util/copy_move.h"

#include <cstdint>

namespace timing {

using doc::TickT;

// Atomically written by audio thread, atomically read by GUI.
// Make sure this fits within 8 bytes.
struct [[nodiscard]] alignas(uint64_t) SequencerTime {
    TickT ticks;
    bool playing;

    constexpr SequencerTime(TickT ticks, bool playing = true)
        : ticks(ticks)
        , playing(playing)
    {}

    constexpr SequencerTime() : SequencerTime(0) {}

    CONSTEXPR_COPY(SequencerTime)
    EQUALABLE(SequencerTime)
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
        return SequencerTime(0, false);
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

    bool has_value() const {
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

#ifdef UNITTEST

#include <fmt/core.h>

#include <ostream>

namespace timing {
    inline std::ostream& operator<< (std::ostream& os, SequencerTime const & value) {
        os << fmt::format("SequencerTime{{{}, {}}}", value.ticks, value.playing);
        return os;
    }
}

#endif
