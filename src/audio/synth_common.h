#pragma once

#include "make_blip_buffer.h"
#include "event_queue.h"
#include "audio_common.h"
#include "util/enum_map.h"
#include "util/macros.h"

#include <gsl/gsl>
#include <boost/core/noncopyable.hpp>

#include <cassert>
#include <vector>

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

using ChannelToNesChip = EnumMap<ChannelID, NesChipID>;
extern const ChannelToNesChip CHANNEL_TO_NES_CHIP;


struct RegisterWrite {
    Address address;
    Byte value;
};

/// Sound chip base class for NES chips and expansions.
/// Other consoles may use a different base class (SNES) or maybe not (wavetable chips).
class BaseNesSynth : boost::noncopyable {
public:
    virtual ~BaseNesSynth() {}

    virtual void write_memory(RegisterWrite write) = 0;

    /// If the synth generates audio via Blip_Synth, nsamp_returned == 0.
    /// If the synth writes audio into `write_buffer`,
    /// nsamp_returned == how many samples were written.
    using NsampWritten = blip_nsamp_t;

    /// Most NesChipSynth subclasses will write to a Blip_Buffer
    /// (if they have a Blip_Synth with a mutable aliased reference to Blip_Buffer).
    /// The VRC7 will write to a Blip_Synth at high frequency (like Mesen).
    /// The FDS will instead write lowpassed audio to write_buffer.
    virtual NsampWritten synthesize_chip_clocks(
        ClockT clk_offset, ClockT nclk, gsl::span<Amplitude> write_buffer
    ) = 0;
};

/// Maybe just inline the methods, don't move to .cpp?
class RegisterWriteQueue {

public:
    struct RelativeRegisterWrite {
        RegisterWrite write;
        ClockT time_before;
    };

private:
    std::vector<RelativeRegisterWrite> vec;

    struct WriteState {
        ClockT accum_dtime = 0;
        bool pending() const {
            return accum_dtime != 0;
        }
    } input;

    struct ReadState {
        ClockT prev_time = 0;
        size_t index = 0;
        bool pending() const {
            return prev_time != 0 || index != 0;
        }
    } output;

public:
    // impl
    DISABLE_COPY_MOVE(RegisterWriteQueue)

    RegisterWriteQueue() : input{}, output{} {
        vec.reserve(4 * 1024);
    }

    void clear() {
        vec.clear();
        input = {};
        output = {};
    }

    // Called by OverallDriver’s member drivers.

    // Is this a usable API? I don't know.
    // I think music_engine::TimeRef will make it easier to use.
    void add_time(ClockT dtime) {
        input.accum_dtime += dtime;
    }

    void push_write(RegisterWrite val) {
        assert(!output.pending());
        RelativeRegisterWrite relative{.write=val, .time_before=input.accum_dtime};
        input.accum_dtime = 0;

        vec.push_back(relative);
    }

    // Called by Synth.

    /// Returns a nullable pointer to a RelativeRegisterWrite.
    RelativeRegisterWrite * peek_mut() {  // -> &'Self mut RelativeRegisterWrite
        assert(!input.pending());

        if (output.index < vec.size()) {
            return &vec[output.index];
        }

        return nullptr;
    }

    RegisterWrite pop() {
        assert(!input.pending());

        assert(output.index < vec.size());
        RelativeRegisterWrite out = vec[output.index++];
        return out.write;
    }

    size_t num_unread() {
        return vec.size() - output.index;
    }
};

using ChipRegisterWriteQueue = EnumMap<NesChipID, RegisterWriteQueue>;

// end namespaces
}
}
