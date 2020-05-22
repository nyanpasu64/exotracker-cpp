// Only include in synth/nes_2a03.cpp, not .h.

#pragma once

#include "music_driver_common.h"
#include "sequencer.h"
#include "chip_kinds.h"

#include "util/copy_move.h"
#include "util/macros.h"

#include <cpp11-on-multicore/bitfield.h>

namespace audio::synth::nes_2a03_driver {

using namespace doc::tuning;
using namespace music_driver;
using chip_kinds::Apu1ChannelID;

// Pulse 1/2 driver

/// Convert [byte][bit]Bit indexing to [bit]Bit.
constexpr static int BITS_PER_BYTE = 8;
#define BYTE_BIT(byte, bit) ((byte) * BITS_PER_BYTE + (bit))
#define BYTE(byte) (BITS_PER_BYTE * (byte))

class Apu1PulseDriver {
    using PulseNum = Range<0, 2, uint32_t>;

    PulseNum const _pulse_num;
    Address const _base_address;
    TuningRef _tuning_table;

    bool _first_tick_occurred = false;

    // TODO add InstrEnvelope class
    // with array, index, and "should tick" or "reached end" methods.
    bool _note_active = false;
    int _volume_index = 0;

public:
    // Reading a ADD_BITFIELD_MEMBER does not sign-extended it.
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

    static constexpr int MAX_PERIOD = decltype(Apu1Reg::period_reg)::Maximum;

private:
    Apu1Reg _prev_state = 0;
    Apu1Reg _next_state = 0;

public:
    // impl
    Apu1PulseDriver(PulseNum pulse_num, TuningRef tuning_table) :
        _pulse_num(pulse_num),
        _base_address(0x4000 + 0x4 * pulse_num),
        _tuning_table(tuning_table)
    {}

    // TODO add a $4015 reference parameter,
    // so after Apu1PulseDriver writes to channels,
    // Apu1Driver can toggle hardware envelopes.
    void tick(
        sequencer::EventsRef events, RegisterWriteQueue &/*out*/ register_writes
    );
};

TuningOwned make_tuning_table(
    FrequenciesRef const frequencies,  // cycle/s
    ClockT const clocks_per_second  // clock/s
);

class Apu1Driver {
    // Apu1PulseDriver references _tuning_table, so disable moving this.
    DISABLE_COPY_MOVE(Apu1Driver)

    using ChannelID = Apu1ChannelID;

TEST_PUBLIC:
    ClockT const _clocks_per_sec;
    TuningOwned _tuning_table;

    sequencer::ChipSequencer<ChannelID> _chip_sequencer;

    Apu1PulseDriver _pulse1_driver{0, TuningRef{_tuning_table}};
    Apu1PulseDriver _pulse2_driver{1, TuningRef{_tuning_table}};

public:

    Apu1Driver(ClockT clocks_per_sec, FrequenciesRef frequencies) :
        _clocks_per_sec(clocks_per_sec)
    {
        recompute_tuning(frequencies);
    }

    void recompute_tuning(FrequenciesRef frequencies) {
        _tuning_table = make_tuning_table(frequencies, _clocks_per_sec);
    }

    void driver_tick(
        doc::Document const & document,
        ChipIndex chip_index,
        RegisterWriteQueue &/*out*/ register_writes
    ) {
        EnumMap<ChannelID, sequencer::EventsRef> channel_events =
            _chip_sequencer.sequencer_tick(document, chip_index);

        _pulse1_driver.tick(channel_events[ChannelID::Pulse1], /*mut*/ register_writes);
        _pulse2_driver.tick(channel_events[ChannelID::Pulse2], /*mut*/ register_writes);

        // TODO write $4015 to register_writes, if I ever add envelope functionality.
    }
};


// namespace
}
