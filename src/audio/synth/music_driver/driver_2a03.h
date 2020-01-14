// Only include in synth/nes_2a03.cpp, not .h.

#pragma once

#include "../music_driver_common.h"

#include "util/macros.h"

#include <cpp11-on-multicore/bitfield.h>
#include <gsl/span>

#include <algorithm>
#include <cmath>  // lround

#ifdef UNITTEST
#include <doctest.h>
#endif

namespace audio::synth::music_driver::driver_2a03 {

using namespace doc::tuning;

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
    ) {
        bool new_note = false;

        for (doc::RowEvent event : events) {
            if (event.note.has_value()) {
                doc::Note note = *event.note;

                if (note.is_valid_note()) {
                    _note_active = true;
                    new_note = true;
                    _volume_index = 0;

                    // Changing pitch may write to $4003, which resets phase and creates a click.
                    // There is a way to avoid this click: http://forums.nesdev.com/viewtopic.php?t=231
                    // I did not implement that method, so I get clicks.
                    _next_state.period_reg = _tuning_table[note.value];
                } else {
                    _note_active = false;
                    new_note = false;
                }
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

        /*
        - https://wiki.nesdev.com/w/index.php/APU#Pulse_.28.244000-4007.29
        - https://wiki.nesdev.com/w/index.php/APU_Pulse
        */

        // https://wiki.nesdev.com/w/index.php/APU_Envelope
        // const_vol could be renamed disable_env or deactivate_env.
        _next_state.const_vol = 1;

        // https://wiki.nesdev.com/w/index.php/APU_Length_Counter
        // Length counter is enabled based on $4015, length_halt, AND length.
        // But length_halt=1 also enables envelope looping.

        // Written by FamiTracker's .nsf driver, but not necessary.
        // _next_state.length_halt = 1;
        // _next_state.length = 1;

        // https://wiki.nesdev.com/w/index.php/APU_Sweep
        // >if the negate flag is false, the shift count is zero, and the current period is at least $400, the target period will be large enough to mute the channel.
        // >to fully disable the sweep unit, a program must turn off enable and turn on negate, such as by writing $08.
        _next_state.bytes[1] = 0x08;


        for (Address byte_idx = 0; byte_idx < Apu1Reg::BYTES; byte_idx++) {
            if (
                !_first_tick_occurred
                || _next_state.bytes[byte_idx] != _prev_state.bytes[byte_idx]
            ) {
                auto write = RegisterWrite{
                    .address = (Address) (_base_address + byte_idx),
                    .value = (Byte) (_next_state.bytes[byte_idx])
                };
                register_writes.push_write(write);
            }
        }
        _first_tick_occurred = true;
        _prev_state = _next_state;
        return;
    }
};

/// given: frequency
/// find: period_reg clamped between [$0, $7FF]
static RegisterInt register_quantize(
    FreqDouble const cycles_per_second, ClockT const clocks_per_second
) {
    /*
    period [clock/cycle] = 16 * (period_reg + 1)
    period [clock/s]/[cycle/s] = clocks_per_second / frequency

    16 * (period_reg + 1) = clocks_per_second / frequency   </16, -1>
    period_reg = clocks_per_second / frequency / 16, -clamped 1
    */

    int reg = lround(clocks_per_second / (cycles_per_second * 16) - 1);
    // Clamps to [lo, hi] inclusive.
    reg = std::clamp(reg, 0, Apu1PulseDriver::MAX_PERIOD);
    return reg;
}

#ifdef UNITTEST
TEST_CASE("Ensure register_quantize() produces correct values.") {
    // 0CC-FamiTracker uses 1789773 as the master clock rate.
    // Given A440, it writes $0FD to the APU1 pulse period.
    CHECK(register_quantize(440, 1789773) == 0x0FD);
}
#endif

static TuningOwned make_tuning_table(
    FrequenciesRef const frequencies,  // cycle/s
    ClockT const clocks_per_second  // clock/s
) {
    TuningOwned out;
    for (size_t i = 0; i < doc::CHROMATIC_COUNT; i++) {
        out[i] = register_quantize(frequencies[i], clocks_per_second);
    }
    return out;
}

#ifdef UNITTEST
TEST_CASE("Ensure make_tuning_table() produces only valid register values.") {
    // 0CC-FamiTracker uses 1789773 as the master clock rate.
    // Given A440, it writes $0FD to the APU1 pulse period.
    CHECK(register_quantize(440, 1789773) == 0x0FD);

    FrequenciesOwned freq;
    std::fill(freq.begin(), freq.end(), 1);
    freq[1] = 1'000;
    freq[2] = 1'000'000;
    freq[3] = 1'000'000'000;

    TuningOwned tuning_table = make_tuning_table(freq, 1789773);
    // 2A03 has 11-bit tuning registers.
    for (RegisterInt reg : tuning_table) {
        CHECK(0 <= reg);
        CHECK(reg < (1 << 11));
    }

    // Ensure the clamping edge cases are correct.
    CHECK(tuning_table[0] == (1 << 11) - 1);
    CHECK(tuning_table[3] == 0);
}
#endif

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
