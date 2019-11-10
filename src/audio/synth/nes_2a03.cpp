#include "nes_2a03.h"

#include <nsfplay/xgm/devices/CPU/nes_cpu.h>
#include <nsfplay/xgm/devices/Sound/nes_apu.h>
#include <nsfplay/xgm/devices/Sound/nes_dmc.h>

#include <array>
#include <cassert>

namespace audio {
namespace synth {
namespace nes_2a03 {

// Disable external linkage.
namespace {

const int APU1_RANGE = 100;
const int APU2_RANGE = 100;

const double APU1_VOLUME = 0.0;
const double APU2_VOLUME = 0.0;

// unnamed namespace
}

/// APU1 (2 pulses)
class NesApu1Synth : public BaseNesApu1Synth {
    // NesApu2Synth::apu2 (xgm::NES_DMC) holds a reference to apu1 (xgm::NES_APU).
    xgm::NES_APU apu1;
    MyBlipSynth apu1_synth;

    friend class NesApu2Synth;

public:
    explicit NesApu1Synth(Blip_Buffer & blip) :
        apu1_synth{blip, APU1_RANGE, APU1_VOLUME}
    {
        apu1.Reset();
    }

    // impl NesChipSynth
    void write_memory(RegisterWrite write) override {
        apu1.Write(write.address, write.value);
    }

    NsampWritten synthesize_chip_clocks(
        ClockT clk_offset, ClockT nclk, gsl::span<Amplitude> write_buffer
    ) override {
        std::array<xgm::INT32, 2> stereo_out;

        // Will running apu1 and apu2 in separate loops improve locality of reference?
        for (ClockT clock = 0; clock < nclk; clock++) {
            apu1.Tick(1);
            apu1.Render(&stereo_out[0]);
            apu1_synth.update((blip_nclock_t) (clk_offset + clock), stereo_out[0]);
        }

        return 0;
    }
};

std::unique_ptr<BaseNesApu1Synth> make_NesApu1Synth(Blip_Buffer & blip) {
    return std::make_unique<NesApu1Synth>(blip);
}

/// APU2 (tri noise dpcm)
///
/// Requirement: NesApu2Synth must be destroyed before NesApu1Synth.
///
/// This is because NesApu2Synth.cpu (xgm::NES_CPU) holds a reference to xgm::NES_APU
/// owned by NesApu1Synth.
/// In C++, arrays are destroyed in reverse order, so this can be guaranteed
/// if the array of unique_ptr<BaseNesSynth> stores NesApu2Synth after NesApu1Synth.
class NesApu2Synth : public BaseNesSynth {
    xgm::NES_DMC apu2;
    MyBlipSynth apu2_synth;

    // xgm::NES_DMC holds references to NES_CPU and NES_APU.
    // We own NES_CPU.
    // NesApu1Synth owns NES_APU, and our constructor takes a reference to one.
    xgm::NES_CPU cpu;

public:
    explicit NesApu2Synth(Blip_Buffer & blip, NesApu1Synth & apu1) :
        apu2_synth{blip, APU2_RANGE, APU2_VOLUME}
    {
        apu2.Reset();

        apu2.SetCPU(&cpu);
        apu2.SetAPU(&(apu1.apu1));
    }

    // impl NesChipSynth
    void write_memory(RegisterWrite write) override {
        apu2.Write(write.address, write.value);
    }

    NsampWritten synthesize_chip_clocks(
        ClockT clk_offset, ClockT nclk, gsl::span<Amplitude> write_buffer
    ) override {
        std::array<xgm::INT32, 2> stereo_out;

        for (ClockT clock = 0; clock < nclk; clock++) {
            apu2.Tick(1);
            apu2.Render(&stereo_out[0]);
            apu2_synth.update((blip_nclock_t) (clk_offset + clock), stereo_out[0]);
        }

        return 0;
    }
};

std::unique_ptr<BaseNesSynth> make_NesApu2Synth(
    Blip_Buffer & blip, BaseNesApu1Synth & apu1
) {
    // honestly static_cast is good enough,
    // as there are no other subclasses of BaseNesApu1Synth
    // which override pure-virtual synthesize_chip_clocks().
    NesApu1Synth * apu1_real = dynamic_cast<NesApu1Synth *>(&apu1);
    assert(apu1_real != nullptr);

    return std::make_unique<NesApu2Synth>(blip, *apu1_real);
}

// End namespaces
}
}
}
