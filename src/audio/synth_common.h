#pragma once

#include "make_blip_buffer.h"
#include "event_queue.h"
#include "audio_common.h"

#include <gsl/gsl>
#include <boost/core/noncopyable.hpp>

namespace audio {
namespace synth {

// https://wiki.nesdev.com/w/index.php/CPU
// >Emulator authors may wish to emulate the NTSC NES/Famicom CPU at 21441960 Hz...
// >to ensure a synchronised/stable 60 frames per second.[2]
// int const MASTER_CLK_PER_S = 21'441'960;

// Except 2A03 APU operates off CPU clock (master clock / 12 if NTSC).
// https://wiki.nesdev.com/w/index.php/FDS_audio
// FDS also operates off CPU clock, despite 0CC storing master clock in a constant.
int const CPU_CLK_PER_S = 1'786'830;

// NTSC is approximately 60 fps.
int const TICKS_PER_S = 60;


/// This type is used widely, so import to audio::synth.
using event_queue::ClockT;

using Address = uint16_t;
using Byte = uint8_t;

struct RegisterWrite {
    Address address;
    Byte value;
};

/// Sound chip base class for NES chips and expansions.
/// Other consoles may use a different base class (SNES) or maybe not (wavetable chips).
class BaseNesSynth : boost::noncopyable {
public:
    virtual ~BaseNesSynth() {}

    struct SynthResult {
        bool wrote_audio;

        // blip_buffer uses signed int for nsamp.
        blip_nsamp_t nsamp_returned;
    };

    /// Most NesChipSynth subclasses will write to a Blip_Buffer
    /// (if they have a Blip_Synth with a mutable aliased reference to Blip_Buffer).
    /// The VRC7 will write to a Blip_Synth at high frequency (like Mesen).
    /// The FDS will instead write lowpassed audio to write_buffer.
    virtual SynthResult synthesize_chip_clocks(
        ClockT nclk, gsl::span<Amplitude> write_buffer
    ) = 0;
};

// end namespaces
}
}
