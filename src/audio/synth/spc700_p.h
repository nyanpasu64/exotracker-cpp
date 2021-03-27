#pragma once

#include "spc700.h"
#include "../synth_common.h"

#include <snes9x-dsp/SPC_DSP.h>

#include <cstdint>

namespace audio::synth::spc700_driver {
    class Spc700Driver;
}

namespace audio::synth::spc700 {

constexpr size_t SPC_MEMORY_SIZE = 0x1'0000;

class Spc700Synth {
    uint8_t _ram_64k[SPC_MEMORY_SIZE] = {};
    SPC_DSP _chip;

// impl
public:
    Spc700Synth();

    void write_memory(RegisterWrite write);

    ChipInstance::NsampWritten run_clocks(
        ClockT const nclk,
        WriteTo write_to);

    /// The driver needs to be able to manipulate RAM and emulator state directly,
    /// when editing the sample layout.
    friend class spc700_driver::Spc700Driver;
};


}
