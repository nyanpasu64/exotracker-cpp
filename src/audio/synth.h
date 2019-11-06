#pragma once

#include "synth/nes_2a03.h"
#include "synth_common.h"
#include "audio_common.h"
#include "util/enum_map.h"

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
enum class NesChipID {
    NesApu1,
    NesApu2,

    COUNT,
    NotNesChip,
};

/// List of sound channels, belonging to chips.
enum class ChannelID {
    // NesApu1
    Pulse1,
    Pulse2,

    // NesApu2
    Tri,
    Noise,
    Dpcm,

    COUNT,
};

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

    EnumMap<NesChipID, bool> _chip_active = {};
    EnumMap<NesChipID, std::unique_ptr<BaseNesSynth>> _chip_synths = {};

public:
    OverallSynth(OverallSynth &&) = default;

    OverallSynth(int stereo_nchan, int smp_per_s);

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
