#pragma once

#include "../synth_common.h"

namespace audio {
namespace synth {
namespace nes_2a03 {

class Nes2A03Synth : public NesChipSynth {

public:
    explicit Nes2A03Synth(Blip_Buffer & blip) {}

    // impl NesChipSynth
    SynthResult synthesize_chip_cycles(
            CycleT ncyc, gsl::span<Amplitude> write_buffer
            ) override;
};

// End namespaces
}
}
}

