// Only include in synth/nes_2a03.cpp, not .h.

#pragma once

#include "../music_driver_common.h"

#include <cpp11-on-multicore/bitfield.h>

#include <memory>

namespace audio::synth::music_driver::driver_2a03 {

// Pulse 1/2 driver

/// Convert [byte][bit]Bit indexing to [bit]Bit.
constexpr static int BITS_PER_BYTE = 8;
#define BYTE_BIT(byte, bit) ((byte) * BITS_PER_BYTE + (bit))
#define BYTE(byte) (BITS_PER_BYTE * (byte))

class Apu1PulseDriver {
    using PulseNum = Range<0, 2, uint32_t>;

    PulseNum const _pulse_num;
    Address const _base_address;

    // TODO add InstrEnvelope class
    // with array, index, and "should tick" or "reached end" methods.
    bool _note_active = false;
    int _volume_index = 0;

    // Reading a ADD_BITFIELD_MEMBER does not sign-extended it.
    // The read value can only be negative
    // if the ADD_BITFIELD_MEMBER has the same length as the storage type
    // (AKA the type returned by member accesses).
    BEGIN_BITFIELD_TYPE(Apu1Reg, int32_t)
        // Access bit fields.
    //  ADD_BITFIELD_MEMBER(memberName,     offset,         bits)
        ADD_BITFIELD_MEMBER(volume,         BYTE(0) + 0,    4)
        ADD_BITFIELD_MEMBER(lol_const_vol,  BYTE(0) + 4,    1)
        ADD_BITFIELD_MEMBER(lol_halt,       BYTE(0) + 5,    1)
        ADD_BITFIELD_MEMBER(duty,           BYTE(0) + 6,    2)

        ADD_BITFIELD_MEMBER(period_minus_1, BYTE(2) + 0,    BYTE(1) + 3)
        ADD_BITFIELD_MEMBER(lol_length,     BYTE(3) + 3,    5)

        // Access raw bytes in endian-independent fashion.
        constexpr static Address BYTES = 4;
        ADD_BITFIELD_ARRAY(bytes, /*offset*/ 0, /*bits*/ 8, /*numItems*/ BYTES)
    END_BITFIELD_TYPE()

    Apu1Reg _prev_state = 0;
    Apu1Reg _next_state = 0;

public:
    Apu1PulseDriver(PulseNum pulse_num) :
        _pulse_num(pulse_num),
        _base_address(0x4000 + 0x4 * pulse_num)
    {}

    // TODO add a $4015 reference parameter,
    // so after Apu1PulseDriver writes to channels,
    // Apu1Driver can toggle hardware envelopes.
    void tick(
        sequencer::EventsRef const events, RegisterWriteQueue &/*out*/ register_writes
    ) {
        bool new_note = false;

        for (doc::RowEvent event : events) {
            if (event.note.has_value()) {
                _volume_index =  0;
                _note_active = true;
                new_note = true;
            }
        }

        int volume;
        if (_note_active) {
            volume = 0xc - (_volume_index / 1) - 3 * _pulse_num;

            if (!new_note) {
                // Advance envelope. TODO move to InstrEnvelope or SynthEnvelope class.
                if (volume > 0) {
                    _volume_index += 1;
                }
            }
        } else {
            volume = 0;
        }

        _next_state.volume = volume;
        _next_state.duty = 0x1 + _pulse_num;
        _next_state.period_minus_1 = 0x1ab;

        /*
        i don't know why this works, but it's what 0cc .nsf does.
        imo these registers are useless in famitracker-style music.

        - https://wiki.nesdev.com/w/index.php/APU#Pulse_.28.244000-4007.29
        - https://wiki.nesdev.com/w/index.php/APU_Pulse
        */

        // https://wiki.nesdev.com/w/index.php/APU_Envelope
        _next_state.lol_const_vol = 1;

        // https://wiki.nesdev.com/w/index.php/APU_Length_Counter
//        _next_state.lol_halt = 1;
//        _next_state.lol_length = 1;

        // https://wiki.nesdev.com/w/index.php/APU_Sweep
//        _next_state.bytes[1] = 0x08;


        for (Address byte_idx = 0; byte_idx < Apu1Reg::BYTES; byte_idx++) {
            if (_next_state.bytes[byte_idx] != _prev_state.bytes[byte_idx]) {
                auto write = RegisterWrite{
                    .address = (Address) (_base_address + byte_idx),
                    .value = (Byte) (_next_state.bytes[byte_idx])
                };
                register_writes.push_write(write);
            }
        }
        _prev_state = _next_state;
        return;
    }
};

class Apu1Driver {
    using ChannelID = Apu1ChannelID;

    sequencer::ChipSequencer<ChannelID> _chip_sequencer;

    Apu1PulseDriver _pulse1_driver{0};
    Apu1PulseDriver _pulse2_driver{1};

public:
    void driver_tick(
        doc::Document & document,
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
