#pragma once

#include "spc700.h"
#include "music_driver_common.h"
#include "../synth_common.h"
#include "util/copy_move.h"

#include <snes9x-dsp/SPC_DSP.h>

#include <cstdint>
#include <memory>

namespace audio::synth::spc700_driver {
    class Spc700Driver;
}

namespace audio::synth::spc700_synth {

using music_driver::RegisterWrite;

constexpr size_t SPC_MEMORY_SIZE = 0x1'0000;

struct Spc700Inner {
    uint8_t ram_64k[SPC_MEMORY_SIZE] = {};
    SPC_DSP chip;

// impl
    /// SPC_DSP is self-referential and points to ram_64k.
    DISABLE_COPY_MOVE(Spc700Inner)

    Spc700Inner();
    void reset();
};

class Spc700Synth {
    std::unique_ptr<Spc700Inner> _p;

// impl
public:
    Spc700Synth();

    void reset();

    /// Write to a S-DSP register (not to ARAM).
    /// (In the actual SNES, this corresponds to a $F2 write followed by $F3.)
    /// (Writing sample data should be accomplished by mutating _ram_64k directly.)
    void write_reg(RegisterWrite write);

    NsampWritten run_clocks(
        ClockT const nclk,
        WriteTo write_to);

    uint8_t * ram_64k() {
        return _p->ram_64k;
    }
};

}
