#include "nes_2a03.h"

namespace audio {
namespace synth {
namespace nes_2a03 {


NesChipSynth::SynthResult Nes2A03Synth::synthesize_chip_cycles(
        CycleT ncyc, gsl::span<Amplitude> write_buffer
        ) {
    return SynthResult{.wrote_audio=false, .nsamp_returned=0};
}

// End namespaces
}
}
}
