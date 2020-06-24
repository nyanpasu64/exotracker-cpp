#pragma once

#include "doc.h"

#include <cstdint>
#include <optional>

namespace timing {

// Atomically written by audio thread, atomically read by GUI.
// Make sure this fits within 8 bytes.
struct [[nodiscard]] alignas(uint64_t) SequencerTime {
    /// The top bit is reserved for FlagAndTime,
    /// and will be unset in MaybeSequencerTime::none().
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

    constexpr SequencerTime() : SequencerTime{0, 0, 0, 0} {}

    CONSTEXPR_COPY(SequencerTime)
    EQUALABLE(SequencerTime, (seq_entry_index, curr_ticks_per_beat, beats, ticks))
};
static_assert(sizeof(SequencerTime) <= 8, "SequencerTime over 8 bytes, not atomic");


static constexpr uint16_t TOP_BIT = 0x8000;


struct [[nodiscard]] MaybeSequencerTime {
private:
    SequencerTime _timestamp;

public:
    constexpr MaybeSequencerTime(SequencerTime timestamp)
        : _timestamp{timestamp}
    {}

    static constexpr MaybeSequencerTime none() {
        return SequencerTime{(uint16_t) ~TOP_BIT, (uint16_t) -1, -1, -1};
    }

    bool has_value() const {
        return *this != none();
    }

    SequencerTime const & get() const {
        return _timestamp;
    }

    SequencerTime & get() {
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

    EQUALABLE(MaybeSequencerTime, (_timestamp))
};


/// A sequencer timestamp read and written by GUI and audio threads.
/// See DESIGN.md "GUI/audio communication".
struct [[nodiscard]] FlagAndTime {
private:
    MaybeSequencerTime _flag__maybe_timestamp;

    static void
    ts_set_is_playing(MaybeSequencerTime & timestamp, bool is_playing) {
        if (is_playing) {
            timestamp->seq_entry_index |= TOP_BIT;
        } else {
            timestamp->seq_entry_index &= (uint16_t) ~TOP_BIT;
        }
    }

public:
    constexpr FlagAndTime(bool is_playing, MaybeSequencerTime maybe_timestamp)
        : _flag__maybe_timestamp{maybe_timestamp}
    {
        set_is_playing(is_playing);
    }

    bool was_playing() const {
        return _flag__maybe_timestamp->seq_entry_index & TOP_BIT;
    }

    void set_is_playing(bool is_playing) {
        ts_set_is_playing(_flag__maybe_timestamp, is_playing);
    }

    MaybeSequencerTime maybe_time() const {
        auto timestamp = _flag__maybe_timestamp;
        ts_set_is_playing(timestamp, false);
        return timestamp;
    }

    bool has_time() const {
        return maybe_time().has_value();
    }

    void set_maybe_time(MaybeSequencerTime time) {
        bool playing = was_playing();
        _flag__maybe_timestamp = time;
        set_is_playing(playing);
    }

    void set_time_none() {
        set_maybe_time(MaybeSequencerTime::none());
    }

    // Too confusing.
//    SequencerTime get_time() const {
//        return maybe_time().get();
//    }

    EQUALABLE(FlagAndTime, (_flag__maybe_timestamp))
};
static_assert(sizeof(FlagAndTime) <= 8, "FlagAndTime over 8 bytes, not atomic");

}
