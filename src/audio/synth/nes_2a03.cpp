#include "nes_2a03.h"
#include "music_driver/driver_2a03.h"
#include "util.h"
#include "util/macros.h"

#include <nsfplay/xgm/devices/CPU/nes_cpu.h>
#include <nsfplay/xgm/devices/Sound/nes_apu.h>
#include <nsfplay/xgm/devices/Sound/nes_dmc.h>

#include <array>

namespace audio {
namespace synth {
namespace nes_2a03 {

// Disable external linkage.
namespace {

// APU1 single pulse wave playing at volume F produces values 0 and 1223.
const int APU1_RANGE = 3000;
const int APU2_RANGE = 100;

const double APU1_VOLUME = 0.5;
const double APU2_VOLUME = 0.0;

// unnamed namespace
}

/// APU1 (2 pulses)
template<typename Synth = MyBlipSynth>
class Apu1Instance : public BaseApu1Instance {
public:
    // types
    STATIC(ChipKind chip_kind(), ChipKind::Apu1)
    using ChannelID = Apu1ChannelID;

private:
    // fields
    music_driver::driver_2a03::Apu1Driver _driver;

    // NesApu2Synth::apu2 (xgm::NES_DMC) holds a reference to apu1 (xgm::NES_APU).
    xgm::NES_APU _apu1;
    Synth _apu1_synth;

    /// Must be 1 or greater.
    /// Increasing it past 1 causes sound synths to only be sampled
    /// (sent to Blip_Buffer) every n clocks.
    ///
    /// This is a performance improvement because nsfplay's audio synths
    /// must be advanced before I can take an audio sample.
    ///
    /// FamiTracker's 2A03 synths can advance in time to the next output transition,
    /// eliminating the need to sample it thousands/millions of times a second.
    /// They eat negligible CPU unless you give it very short periods
    /// (like triangle register 0).
    ClockT const _clocks_per_smp;
    ClockT _clocks_after_tick = 0;

    // friend class NesApu2Synth;

public:
    explicit Apu1Instance(
        Blip_Buffer & blip,
        ClockT clocks_per_sec,
        doc::FrequenciesRef frequencies,
        ClockT clocks_per_sound_update
    ) :
        _driver{clocks_per_sec, frequencies},
        _apu1_synth{blip, APU1_RANGE, APU1_VOLUME},
        _clocks_per_smp{clocks_per_sound_update}
    {
        // Make sure these parameters aren't swapped.
        release_assert(clocks_per_sound_update < clocks_per_sec);
        _apu1.Reset();
    }

    // impl ChipInstance
    void driver_tick(
        doc::Document & document, chip_kinds::ChipIndex chip_index
    ) override {
        // Sequencer's time passes.
        _driver.driver_tick(document, chip_index, /*out*/ _register_writes);
    }

    void synth_write_memory(RegisterWrite write) override {
        _apu1.Write(write.address, write.value);
    }

    /// Intended guarantees:
    /// - _apu1_synth's amplitude is only ever updated at multiples of _clock_increment.
    /// - After this method returns, _apu1 has advanced exactly nclk cycles.
    ///
    /// This ensures:
    /// - Audio updates occur at exactly uniform intervals.
    /// - Subsequent register writes occur exactly at the right time.
    ///
    /// It really doesn't matter, but I'm a perfectionist.
    /// This should be verified through unit testing.
    NsampWritten synth_run_clocks(
        ClockT const clk_begin, ClockT const nclk, gsl::span<Amplitude> write_buffer
    ) override {

        release_assert(_clocks_per_smp > 0);

        /*
        _apu1.Tick(dclk) runs the chip,
        then writes audio into an internal stereo [2]amplitude.
        This makes it impossible to identify the audio level at time 0,
        unless you do something questionable like Tick(0).

        advance(dclk) calls _apu1.Tick(dclk).
        take_sample() calls _apu1.Render(out array).
        */

        ClockT clock = 0;
        auto advance = [&](ClockT dclk) {
            _apu1.Tick(dclk);
            clock += dclk;
        };

        auto advance_maybe = [&](ClockT dclk) {
            if (dclk > 0) {
                advance(dclk);
            }
        };

        std::array<xgm::INT32, 2> stereo_out;

        /// Outputs audio from internal stereo [2]amplitude.
        auto take_sample = [&]() {
            _apu1.Render(&stereo_out[0]);
            _apu1_synth.update((blip_nclock_t) (clk_begin + clock), stereo_out[0]);
        };

        if (_clocks_per_smp <= 1) {
            // Will running apu1 and apu2 in separate loops improve locality of reference?
            while (clock < nclk) {
                advance(1);
                take_sample();
            }
        } else {
            ClockT clocks_until_smp;

            // TODO add test where you increment by 1 clock when _clocks_per_smp is 4.
            // In that edge case (which comes up after 5 seconds of regular operation),
            // even advance(_clocks_after_tick) will overshoot nclk.
            if (nclk < _clocks_after_tick) {
                // Don't sample the synth at all. Put it off.
                clocks_until_smp = _clocks_after_tick;
            } else {
                // This will not take a sample at t=0.
                // This is deliberate. See the above comment about _apu1.Tick.
                if (_clocks_after_tick > 0) {
                    advance(_clocks_after_tick);
                    take_sample();
                }
                _clocks_after_tick = _clocks_per_smp;

                while (clock + _clocks_per_smp <= nclk) {
                    advance(_clocks_per_smp);
                    take_sample();
                }
                clocks_until_smp = _clocks_per_smp;
            }

            ClockT clocks_before_tick = util::distance(clock, nclk);
            release_assert(
                0 <= clocks_before_tick && clocks_before_tick < clocks_until_smp
            );

            advance_maybe(clocks_before_tick);
            release_assert(clock == nclk);
            // do NOT call take_sample(). That will happen after _clocks_after_tick.

            _clocks_after_tick = clocks_until_smp - clocks_before_tick;
            release_assert(clocks_before_tick + _clocks_after_tick == _clocks_per_smp);
        }

        return 0;
    }
};

std::unique_ptr<BaseApu1Instance> make_Apu1Instance(
    Blip_Buffer & blip,
    ClockT clocks_per_sec,
    doc::FrequenciesRef frequencies,
    ClockT clocks_per_sound_update
) {
    return std::make_unique<Apu1Instance<>>(
        blip, clocks_per_sec, frequencies, clocks_per_sound_update
    );
}

///// APU2 (tri noise dpcm)
/////
///// Requirement: NesApu2Synth must be destroyed before NesApu1Synth.
/////
///// This is because NesApu2Synth.cpu (xgm::NES_CPU) holds a reference to xgm::NES_APU
///// owned by NesApu1Synth.
///// In C++, arrays are destroyed in reverse order, so this can be guaranteed
///// if the array of unique_ptr<BaseNesSynth> stores NesApu2Synth after NesApu1Synth.
//class NesApu2Synth : public BaseChipSynth {
//    xgm::NES_DMC apu2;
//    MyBlipSynth apu2_synth;

//    // xgm::NES_DMC holds references to NES_CPU and NES_APU.
//    // We own NES_CPU.
//    // NesApu1Synth owns NES_APU, and our constructor takes a reference to one.
//    xgm::NES_CPU cpu;

//public:
//    explicit NesApu2Synth(Blip_Buffer & blip, Apu1Instance & apu1) :
//        apu2_synth{blip, APU2_RANGE, APU2_VOLUME}
//    {
//        apu2.Reset();

//        apu2.SetCPU(&cpu);
//        apu2.SetAPU(&(apu1.apu1));
//    }

//    // impl NesChipSynth
//    void write_memory(RegisterWrite write) override {
//        apu2.Write(write.address, write.value);
//    }

//    NsampWritten synthesize_chip_clocks(
//        ClockT clk_offset, ClockT nclk, gsl::span<Amplitude> write_buffer
//    ) override {
//        std::array<xgm::INT32, 2> stereo_out;

//        for (ClockT clock = 0; clock < nclk; clock++) {
//            apu2.Tick(1);
//            apu2.Render(&stereo_out[0]);
//            apu2_synth.update((blip_nclock_t) (clk_offset + clock), stereo_out[0]);
//        }

//        return 0;
//    }
//};

//std::unique_ptr<BaseChipSynth> make_NesApu2Synth(
//    Blip_Buffer & blip, BaseNesApu1Synth & apu1
//) {
//    // honestly static_cast is good enough,
//    // as there are no other subclasses of BaseNesApu1Synth
//    // which override pure-virtual synthesize_chip_clocks().
//    Apu1Instance * apu1_real = dynamic_cast<Apu1Instance *>(&apu1);
//    assert(apu1_real != nullptr);

//    return std::make_unique<NesApu2Synth>(blip, *apu1_real);
//}

// End namespaces
}
}
}
