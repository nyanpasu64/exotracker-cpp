#include "nes_2a03.h"
#include "nes_2a03_driver.h"
#include "nes_instance_common.h"
#include "sequencer.h"
#include "audio/event_queue.h"
#include "chip_kinds.h"
#include "timing_common.h"
#include "util/copy_move.h"
#include "util/distance.h"
#include "util/release_assert.h"

#include <nsfplay/xgm/devices/CPU/nes_cpu.h>
#include <nsfplay/xgm/devices/Sound/nes_apu.h>
#include <nsfplay/xgm/devices/Sound/nes_dmc.h>

#include <array>

namespace audio {
namespace synth {
namespace nes_2a03 {

using nes_2a03_driver::Apu1Driver;
using nes_2a03_driver::Apu2Driver;
using nes_2a03_driver::NesDriver;
using nes_instance::ImplChipInstance;
using sequencer::ChipSequencer;
using timing::SequencerTime;
using chip_kinds::Apu1ChannelID;
using chip_kinds::NesChannelID;


// APU1 single pulse wave playing at volume F produces values 0 and 1223.
constexpr int APU1_RANGE = 3000;
// Selected ad-hoc.
constexpr int NES_RANGE = 6000;

constexpr double VOLUME = 0.5;


enum class SampleEvent {
    // EndOfCallback comes before Tick.
    // If a callback ends at the same time as a tick occurs,
    // the tick should happen next callback.
    EndOfCallback,
    Sample,
    COUNT,
};

/// Not a fan of NSFPlay.
///
/// NSFPlay chips have an odd stereo setup (we don't support stereo now).
/// They can only be sampled (at user-determined times),
/// and cannot be queried for the time of the next level transition
/// (sampling interval is a speed-quality tradeoff).
///
/// This class samples a NSFPlay chip at a uniform interval,
/// and forwards the values into a Blip_Synth.
template<typename SoundChip, int RANGE>  // same interface as NSFPlay's ISoundChip
class Synth {
    SoundChip _chip;
    MyBlipSynth _blip_synth;

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
    EventQueue<SampleEvent> _pq;

// impl
public:
    Synth(ClockT clocks_per_sound_update)
        : _blip_synth(VOLUME, RANGE)
        , _clocks_per_smp(clocks_per_sound_update)
    {
        // Sanity check.
        release_assert(clocks_per_sound_update < 100);
        _chip.Reset();

        // Cancel out DC. Necessary for nsfplay APU2.
        {
            // Tick(0) is valid. https://github.com/bbbradsmith/nsfplay/commit/14cb23159584427053a6e5456bb1f9ce8d0918d5
            _chip.Tick(0);

            std::array<xgm::INT32, 2> stereo_out;
            _chip.Render(&stereo_out[0]);
            _blip_synth.center_dc(stereo_out[0]);
        }

        // Do *not* sample at t=0.
        // You must run nsfplay synth to produce audio to sample.
        _pq.set_timeout(SampleEvent::Sample, _clocks_per_smp);
    }

    void write_memory(RegisterWrite write) {
        _chip.Write(write.address, write.value);
    }

    /// Intended guarantees:
    /// - _blip_synth's amplitude is only ever updated at multiples of _clock_increment.
    /// - After this method returns, _chip has advanced exactly nclk cycles.
    ///
    /// This ensures:
    /// - Audio updates occur at exactly uniform intervals.
    /// - Subsequent register writes occur exactly at the right time.
    ///
    /// It really doesn't matter, but I'm a perfectionist.
    /// This should be verified through unit testing.
    ChipInstance::NsampWritten run_clocks(
        ClockT const clk_begin,
        ClockT const nclk,
        gsl::span<Amplitude>,
        Blip_Buffer & blip)
    {
        release_assert(_clocks_per_smp > 0);

        /*
        _chip.Tick(dclk) runs the chip,
        then writes audio into an internal stereo [2]amplitude.
        This makes it impossible to identify the audio level at time 0,
        unless you do something questionable like Tick(0).

        advance(dclk) calls _chip.Tick(dclk).
        take_sample() calls _chip.Render(out array).
        */

        ClockT clock = 0;
        auto advance = [&](ClockT dclk) {
            _chip.Tick(dclk);
            clock += dclk;
        };

        /// Outputs audio from internal stereo [2]amplitude.
        auto take_sample = [&]() {
            std::array<xgm::INT32, 2> stereo_out;
            _chip.Render(&stereo_out[0]);
            _blip_synth.update(
                (blip_nclock_t) (clk_begin + clock), stereo_out[0], &blip
            );
        };

        if (_clocks_per_smp <= 1) {
            while (clock < nclk) {
                advance(1);
                take_sample();
            }
        } else {
            _pq.set_timeout(SampleEvent::EndOfCallback, nclk);
            while (true) {
                auto ev = _pq.next_event();
                if (ev.clk_elapsed > 0) {
                    advance(ev.clk_elapsed);
                }

                SampleEvent id = ev.event_id;
                switch (id) {
                    case SampleEvent::EndOfCallback: {
                        release_assert(clock == nclk);
                        goto end_while;
                    }

                    case SampleEvent::Sample: {
                        take_sample();
                        _pq.set_timeout(SampleEvent::Sample, _clocks_per_smp);
                        break;
                    }
                    case SampleEvent::COUNT: break;
                }
            }
        }

        end_while:
        return 0;  // Wrote 0 bytes to write_buffer.
    }
};

using Apu1Synth = Synth<xgm::NES_APU, APU1_RANGE>;
using Apu1Instance = ImplChipInstance<Apu1Driver, Apu1Synth>;

std::unique_ptr<ChipInstance> make_Apu1Instance(
    chip_common::ChipIndex chip_index,
    ClockT clocks_per_sec,
    doc::FrequenciesRef frequencies,
    ClockT clocks_per_sound_update
) {
    return std::make_unique<Apu1Instance>(
        chip_index,
        Apu1Driver(clocks_per_sec, frequencies),
        Apu1Synth(clocks_per_sound_update));
}

/// Full NES 2A03 sound chip (APU1+APU2).
class NesSoundChip {
private:
    using ChannelID = NesChannelID;

// fields
    // Synth objects
    xgm::NES_APU _apu1;

    // xgm::NES_DMC holds references to xgm::NES_APU.
    // As a result, _apu2 is stored after _apu1, so _apu2 gets destroyed first.
    xgm::NES_DMC _apu2;

// impl
public:
    DISABLE_COPY(NesSoundChip)

    /// _apu2 points to _apu1.
    /// When move-constructing NesSoundChip, fix the new _apu2's pointer.
    /// This is a non-owning pointer, so no double-free occurs when `other` is destroyed.
    NesSoundChip(NesSoundChip && other) : _apu1(other._apu1), _apu2(other._apu2) {
        // Post-move hook, not initial constructor.
        _apu2.SetAPU(&_apu1);
    }

    /// Not necessary to implement this.
    NesSoundChip & operator=(NesSoundChip &&) = delete;

    // Constructors
    explicit NesSoundChip() {
        // Disable nondeterministic behavior.
        // OPT_RANDOMIZE_TRI also causes a pop when playback begins.
        // This could be fixed someday by adding a Blip_Synth method
        // to not generate sound on the first update.
        _apu2.SetOption(xgm::NES_DMC::OPT_RANDOMIZE_TRI, 0);
        _apu2.SetOption(xgm::NES_DMC::OPT_RANDOMIZE_NOISE, 0);
    }

    void Reset() {
        _apu2.SetAPU(&_apu1);

        _apu1.Reset();
        _apu2.Reset();
    }

    bool Write (uint32_t adr, uint32_t val) {
        bool out = false;

        // not short-circuiting
        out |= _apu1.Write(adr, val);
        out |= _apu2.Write(adr, val);

        return out;
    }

    void Tick(uint32_t clocks) {
        // may call FrameSequence() on both APU1 and APU2.
        _apu2.TickFrameSequence(clocks);

        _apu1.Tick(clocks);
        _apu2.Tick(clocks);
    }

    void Render(int32_t out[2]) {
        int32_t temp[2];

        _apu1.Render(out);

        _apu2.Render(temp);
        out[0] += temp[0];
        out[1] += temp[1];
    }
};

using NesSynth = Synth<NesSoundChip, NES_RANGE>;
using NesInstance = ImplChipInstance<NesDriver, NesSynth>;

std::unique_ptr<ChipInstance> make_NesInstance(
    chip_common::ChipIndex chip_index,
    ClockT clocks_per_sec,
    doc::FrequenciesRef frequencies,
    ClockT clocks_per_sound_update)
{
    return std::make_unique<NesInstance>(
        chip_index,
        NesDriver(clocks_per_sec, frequencies),
        NesSynth(clocks_per_sound_update));
}

// End namespaces
}
}
}
