// Only include in synth/nes_2a03.cpp, not .h.

#pragma once

#include "music_driver_common.h"
#include "envelope.h"
#include "sequencer.h"
#include "doc.h"
#include "chip_kinds.h"

#include "util/copy_move.h"
#include "util/macros.h"

#include <cpp11-on-multicore/bitfield.h>

namespace audio::synth::nes_2a03_driver {

using namespace doc::tuning;
using namespace music_driver;
using chip_kinds::Apu1ChannelID;

// Pulse 1/2 driver

#define ITER_TYPE(NAME) \
    envelope::EnvelopeIterator<decltype(doc::Instrument::NAME)>

/// Convert [byte][bit]Bit indexing to [bit]Bit.
constexpr static int BITS_PER_BYTE = 8;
#define BYTE_BIT(byte, bit) ((byte) * BITS_PER_BYTE + (bit))
#define BYTE(byte) (BITS_PER_BYTE * (byte))

class Apu1PulseDriver {
// types
    using PulseNum = Range<0, 2, uint16_t>;

    struct Envelopes {
        ITER_TYPE(volume) volume;
        ITER_TYPE(arpeggio) arpeggio;
        ITER_TYPE(wave_index) wave_index;

        Envelopes();

        /// F is a lambda [](auto){} whose operator() is a template
        /// accepting multiple types.
        template<typename Func>
        void foreach(Func f) {
            f(volume);
            f(arpeggio);
            f(wave_index);
        }
    };

    // Reading a ADD_BITFIELD_MEMBER does not sign-extend it.
    // The read value can only be negative
    // if the ADD_BITFIELD_MEMBER has the same length as the storage type
    // (AKA the type returned by member accesses).
    BEGIN_BITFIELD_TYPE(Apu1Reg, int32_t)
        // Access bit fields.
    //  ADD_BITFIELD_MEMBER(memberName,     offset,         bits)
        ADD_BITFIELD_MEMBER(volume,         BYTE(0) + 0,    4)
        ADD_BITFIELD_MEMBER(const_vol,      BYTE(0) + 4,    1)
        ADD_BITFIELD_MEMBER(length_halt,    BYTE(0) + 5,    1)
        ADD_BITFIELD_MEMBER(duty,           BYTE(0) + 6,    2)

        // Period: clock/cycle = (period_reg + 1) * 16
        ADD_BITFIELD_MEMBER(period_reg,     BYTE(2) + 0,    BYTE(1) + 3)
        ADD_BITFIELD_MEMBER(length,         BYTE(3) + 3,    5)

        // Access raw bytes in endian-independent fashion.
        constexpr static Address BYTES = 4;
        ADD_BITFIELD_ARRAY(bytes, /*offset*/ 0, /*bits*/ 8, /*numItems*/ BYTES)
    END_BITFIELD_TYPE()

// constants
public:
    static constexpr int MAX_VOLUME = decltype(Apu1Reg::volume)::Maximum;
    static_assert (MAX_VOLUME == 15, "huh?");
    static constexpr int MAX_PERIOD = decltype(Apu1Reg::period_reg)::Maximum;

// fields
private:
    PulseNum /*const*/ _pulse_num;
    Address /*const*/ _base_address;

    bool _first_tick_occurred = false;

    Envelopes _envs{};

    doc::Note _prev_note = 0;
    int _prev_volume = MAX_VOLUME;

    Apu1Reg _prev_state = 0;
    Apu1Reg _next_state = 0;

// impl
public:
    Apu1PulseDriver(PulseNum pulse_num) noexcept;

    DISABLE_COPY(Apu1PulseDriver)
    DEFAULT_MOVE(Apu1PulseDriver)

    void stop_playback(RegisterWriteQueue &/*mut*/ register_writes);

    // TODO add a $4015 reference parameter,
    // so after Apu1PulseDriver writes to channels,
    // Apu1Driver can toggle hardware envelopes.
    void tick(
        doc::Document const & document,
        TuningRef tuning_table,
        sequencer::EventsRef events,
        RegisterWriteQueue &/*out*/ register_writes
    );
};

TuningOwned make_tuning_table(
    FrequenciesRef const frequencies,  // cycle/s
    ClockT const clocks_per_second  // clock/s
);

class Apu1Driver {
// types
public:
    using ChannelID = Apu1ChannelID;

// fields
TEST_PUBLIC:
    ClockT _clocks_per_sec;
    TuningOwned _tuning_table;

    Apu1PulseDriver _pulse1_driver;
    Apu1PulseDriver _pulse2_driver;

public:
    Apu1Driver(ClockT clocks_per_sec, FrequenciesRef frequencies)
        : _clocks_per_sec(clocks_per_sec)
        , _tuning_table(make_tuning_table(frequencies, clocks_per_sec))
        , _pulse1_driver{0}
        , _pulse2_driver{1}
    {}

    DISABLE_COPY(Apu1Driver)
    DEFAULT_MOVE(Apu1Driver)

    void recompute_tuning(FrequenciesRef frequencies) {
        _tuning_table = make_tuning_table(frequencies, _clocks_per_sec);
    }

    void stop_playback(RegisterWriteQueue &/*mut*/ register_writes) {
        _pulse1_driver.stop_playback(/*mut*/ register_writes);
        _pulse2_driver.stop_playback(/*mut*/ register_writes);
    }

    void driver_tick(
        doc::Document const & document,
        EnumMap<ChannelID, sequencer::EventsRef> const & channel_events,
        RegisterWriteQueue &/*out*/ register_writes
    ) {
        _pulse1_driver.tick(
            document,
            _tuning_table,
            channel_events[ChannelID::Pulse1],
            /*mut*/ register_writes);

        _pulse2_driver.tick(
            document,
            _tuning_table,
            channel_events[ChannelID::Pulse2],
            /*mut*/ register_writes);

        // TODO write $4015 to register_writes, if I ever add envelope functionality.
    }
};


// namespace
}
