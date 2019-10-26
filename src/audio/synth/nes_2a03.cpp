#include "nes_2a03.h"

#include <array>

namespace audio {
namespace synth {
namespace nes_2a03 {

const static int APU1_RANGE = 100;
const static int APU2_RANGE = 100;

const static double APU1_VOLUME = 0.0;
const static double APU2_VOLUME = 0.0;

Nes2A03Synth::Nes2A03Synth(Blip_Buffer &blip) :
    apu1_synth{blip, APU1_RANGE, APU1_VOLUME},
    apu2_synth{blip, APU2_RANGE, APU2_VOLUME}
{
    apu1.Reset();
    apu2.Reset();

    apu2.SetCPU(&cpu);
    apu2.SetAPU(&apu1);
}

NesChipSynth::SynthResult Nes2A03Synth::synthesize_chip_clocks(
        ClockT nclk, gsl::span<Amplitude> write_buffer
        ) {
    std::array<INT32, 2> stereo_out;

    // Will running apu1 and apu2 in separate loops improve locality of reference?
    for (ClockT clock = 0; clock < nclk; clock++) {
        apu1.Tick(1);
        apu1.Render(&stereo_out[0]);
        apu1_synth.update((blip_nclock_t) clock, stereo_out[0]);
    }

    // Make sure there are no copy-paste errors where you mix up apu1 and apu2 ;)
    for (ClockT clock = 0; clock < nclk; clock++) {
        apu2.Tick(1);
        apu2.Render(&stereo_out[0]);
        apu2_synth.update((blip_nclock_t) clock, stereo_out[0]);
    }

    return SynthResult{.wrote_audio=false, .nsamp_returned=0};
}

// End namespaces
}
}
}
