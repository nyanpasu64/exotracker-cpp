#include "nes_2a03.h"

#include <cpp11-on-multicore/bitfield.h>

#include <cassert>
#include <cstdint>

namespace audio::synth::music_engine::nes_2a03 {

// Pulse 1/2 engine

/// Convert [byte][bit]Bit indexing to [bit]Bit.
constexpr int BITS_PER_BYTE = 8;
#define BYTE_BIT(byte, bit) ((byte) * BITS_PER_BYTE + (bit))
#define BYTE(byte) (BITS_PER_BYTE * (byte))

class Apu1PulseEngine : public SubMusicEngine {
    using PulseNum = Range<0, 2, uint32_t>;

    PulseNum const _pulse_num;
    Address const _base_address;

    BEGIN_BITFIELD_TYPE(Apu1Reg, uint32_t)
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
    Apu1PulseEngine(PulseNum pulse_num) :
        _pulse_num(pulse_num),
        _base_address(0x4000 + 0x4 * pulse_num)
    {}

    // impl SubMusicEngine
    void run(RegisterWriteQueue & register_writes) override {
        if (_pulse_num != 0) {
            return;
        }

        _next_state.volume = 0xf;
        _next_state.duty = 0x1;
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

std::unique_ptr<SubMusicEngine> make_Pulse1() {
    return std::make_unique<Apu1PulseEngine>(0);
}

std::unique_ptr<SubMusicEngine> make_Pulse2() {
    return std::make_unique<Apu1PulseEngine>(1);
}

// Triangle engine

class Apu2TriEngine : public SubMusicEngine {
public:
    // impl SubMusicEngine
    void run(RegisterWriteQueue & register_writes) override {
        // do nothing
    }
};

std::unique_ptr<SubMusicEngine> make_Tri() {
    return std::make_unique<Apu2TriEngine>();
}

// Noise engine

class Apu2NoiseEngine : public SubMusicEngine {
public:
    // impl SubMusicEngine
    void run(RegisterWriteQueue & register_writes) override {
        // do nothing
    }
};

std::unique_ptr<SubMusicEngine> make_Noise() {
    return std::make_unique<Apu2NoiseEngine>();
}

// DPCM engine

class Apu2DpcmEngine : public SubMusicEngine {
public:
    // impl SubMusicEngine
    void run(RegisterWriteQueue & register_writes) override {
        // do nothing
    }
};

std::unique_ptr<SubMusicEngine> make_Dpcm() {
    return std::make_unique<Apu2DpcmEngine>();
}

}   // namespace