#pragma once

#include "synth/nes_2a03.h"
#include "synth_common.h"
#include "audio_common.h"

#include <memory>

namespace audio {
namespace synth {

/// States for the synth callback loop.
enum class SynthEvent {
    EndOfCallback,
    Tick,
    ChannelUpdate,
    COUNT
};

/// List of sound chips supported.
namespace NesChipID_ {
enum NesChipID {
    // TODO add chip ZERO???
    NesApu1,
    NesApu2,
    COUNT,
};
}
using NesChipID_::NesChipID;


class OverallSynth : boost::noncopyable {
public:
    int _stereo_nchan;

private:
    /*
    TODO this should be configurable.

    But if const, it must be computed within the initializer list,
    instead of in constructor body.
    And it's hard to put complex computations/expressions there.

    So ideally I'd write a Rust-style static factory method instead,
    and rely on guaranteed copy elision for returning.
    https://jonasdevlieghere.com/guaranteed-copy-elision/#guaranteedcopyelision
    Problem is, you can't call a static factory method
    from an owner (AudioThreadHandle) 's initializer list... :S

    When the sampling rate, stereo_nchan, or tick rate change,
    do we create a new OutputCallback or reconfigure the existing one?

    In OpenMPT, ticks/s can change within a song, so it would need to be a method.
    */
    uint32_t _clocks_per_tick = CPU_CLK_PER_S / TICKS_PER_S;

    EventQueue<SynthEvent> _pq;

    // Member variables

    // Audio written into this and read to output.
    Blip_Buffer _nes_blip;

    // Per-chip "special audio" written into this and read into _nes_blip.
    Amplitude _temp_buffer[1 << 16];

    bool _chip_active[NesChipID::COUNT] = {};
    std::unique_ptr<BaseNesSynth> _chip_synths[NesChipID::COUNT] = {};

public:
    OverallSynth(OverallSynth &&) = default;

    OverallSynth(int stereo_nchan, int smp_per_s) :
        _stereo_nchan(stereo_nchan),
        _nes_blip(smp_per_s, CPU_CLK_PER_S)
    {
        _chip_active[NesChipID::NesApu1] = true;
        auto apu1_unique = nes_2a03::make_NesApu1Synth(_nes_blip);
        auto & apu1 = *apu1_unique;
        _chip_synths[NesChipID::NesApu1] = std::move(apu1_unique);

        _chip_active[NesChipID::NesApu2] = true;
        _chip_synths[NesChipID::NesApu2] = nes_2a03::make_NesApu2Synth(_nes_blip, apu1);

        for (auto & chip_synth : _chip_synths) {
            assert(chip_synth != nullptr);
        }
    }

    /// Generates audio to be sent to an audio output (speaker) or WAV file.
    /// The entire output buffer is written to.
    ///
    /// output must have length length of mono_smp_per_block * stereo_nchan.
    /// It is treated as an array of interleaved samples, [smp#, * nchan + chan#] Amplitude.
    void synthesize_overall(gsl::span<Amplitude> output_buffer, size_t const mono_smp_per_block);
};


// end namespaces
}
}
