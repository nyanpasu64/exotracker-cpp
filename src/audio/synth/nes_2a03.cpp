#include "nes_2a03.h"
#include "nes_2a03_driver.h"
#include "sequencer.h"
#include "audio/event_queue.h"
#include "chip_kinds.h"
#include "timing_common.h"
#include "util/distance.h"
#include "util/release_assert.h"

#include <nsfplay/xgm/devices/CPU/nes_cpu.h>
#include <nsfplay/xgm/devices/Sound/nes_apu.h>
#include <nsfplay/xgm/devices/Sound/nes_dmc.h>

#include <array>

namespace audio {
namespace synth {
namespace nes_2a03 {

using chip_kinds::Apu1ChannelID;
using timing::SequencerTime;


// APU1 single pulse wave playing at volume F produces values 0 and 1223.
constexpr int APU1_RANGE = 3000;

constexpr double APU1_VOLUME = 0.5;
//constexpr double APU2_VOLUME = 0.0;


enum class SampleEvent {
    // EndOfCallback comes before Tick.
    // If a callback ends at the same time as a tick occurs,
    // the tick should happen next callback.
    EndOfCallback,
    Sample,
    COUNT,
};

/// APU1 (2 pulses)
template<typename BlipSynthT = MyBlipSynth>
class Apu1Instance : public BaseApu1Instance {
private:
    using ChannelID = Apu1ChannelID;

// fields
    sequencer::ChipSequencer<ChannelID> _chip_sequencer;
    EnumMap<ChannelID, sequencer::EventsRef> _channel_events;
    nes_2a03_driver::Apu1Driver _driver;

    // NesApu2Synth::apu2 (xgm::NES_DMC) holds a reference to apu1 (xgm::NES_APU).
    xgm::NES_APU _apu1;
    BlipSynthT _apu1_synth;

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

    // friend class NesApu2Synth;

// impl
public:
    explicit Apu1Instance(
        chip_common::ChipIndex chip_index,
        ClockT clocks_per_sec,
        doc::FrequenciesRef frequencies,
        ClockT clocks_per_sound_update
    )
        : _chip_sequencer{chip_index}
        , _driver{clocks_per_sec, frequencies}
        , _apu1_synth{APU1_RANGE, APU1_VOLUME}
        , _clocks_per_smp{clocks_per_sound_update}
    {
        // Make sure these parameters aren't swapped.
        release_assert(clocks_per_sound_update < clocks_per_sec);
        _apu1.Reset();

        // Do *not* sample at t=0.
        // You must run nsfplay synth to produce audio to sample.
        _pq.set_timeout(SampleEvent::Sample, _clocks_per_smp);
    }

// impl ChipInstance
    void seek(doc::Document const & document, timing::GridAndBeat time) override {
        _chip_sequencer.seek(document, time);
    }

    void stop_playback() override {
        _chip_sequencer.stop_playback();

        // May append to _register_writes.
        _driver.stop_playback(_register_writes);
    }

    void tempo_changed(doc::Document const & document) override {
        _chip_sequencer.tempo_changed(document);
    }

    void doc_edited(doc::Document const & document) override {
        _chip_sequencer.doc_edited(document);
    }

    void timeline_modified(doc::Document const & document) override {
        _chip_sequencer.timeline_modified(document);
    }

    /// Ticks sequencer and buffers up events for a subsequent call to driver_tick().
    SequencerTime sequencer_tick(doc::Document const & document) override {
        auto [chip_time, channel_events] = _chip_sequencer.sequencer_tick(document);
        _channel_events = channel_events;
        return chip_time;
    }

    /// Can be called without calling sequencer_tick() first.
    /// This will not play any notes.
    void driver_tick(doc::Document const & document) override {
        // Appends to _register_writes.
        _driver.driver_tick(document, _channel_events, _register_writes);

        // Replace all map values with empty slices.
        _channel_events = {};
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
        ClockT const clk_begin,
        ClockT const nclk,
        gsl::span<Amplitude>,
        Blip_Buffer & blip
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

        std::array<xgm::INT32, 2> stereo_out;

        /// Outputs audio from internal stereo [2]amplitude.
        auto take_sample = [&]() {
            _apu1.Render(&stereo_out[0]);
            _apu1_synth.update(
                (blip_nclock_t) (clk_begin + clock), stereo_out[0], &blip
            );
        };

        if (_clocks_per_smp <= 1) {
            // Will running apu1 and apu2 in separate loops improve locality of reference?
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

std::unique_ptr<BaseApu1Instance> make_Apu1Instance(
    chip_common::ChipIndex chip_index,
    ClockT clocks_per_sec,
    doc::FrequenciesRef frequencies,
    ClockT clocks_per_sound_update
) {
    return std::make_unique<Apu1Instance<>>(
        chip_index, clocks_per_sec, frequencies, clocks_per_sound_update
    );
}

// End namespaces
}
}
}
