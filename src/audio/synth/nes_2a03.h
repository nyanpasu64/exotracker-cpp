#pragma once

#include "../synth_common.h"

#include <nsfplay/xgm/devices/CPU/nes_cpu.h>
#include <nsfplay/xgm/devices/Sound/nes_apu.h>
#include <nsfplay/xgm/devices/Sound/nes_dmc.h>

namespace audio {
namespace synth {
namespace nes_2a03 {

class Nes2A03Synth : public NesChipSynth {
    xgm::NES_CPU cpu;

    /// APU1 (2 pulses)
    xgm::NES_APU apu1;
    /// APU2 (tri noise dpcm)
    xgm::NES_DMC apu2;

    MyBlipSynth apu1_synth;
    MyBlipSynth apu2_synth;

public:
    explicit Nes2A03Synth(Blip_Buffer & blip);

    // impl NesChipSynth
    SynthResult synthesize_chip_clocks(
            ClockT nclk, gsl::span<Amplitude> write_buffer
            ) override;
};

// End namespaces
}
}
}

