#pragma once

#include "make_blip_buffer.h"
#include "event_queue.h"
#include "audio_common.h"
#include "doc.h"
#include "chip_common.h"
#include "timing_common.h"
#include "util/enum_map.h"
#include "util/copy_move.h"

#include <gsl/span>
#include <boost/core/noncopyable.hpp>

#include <vector>

namespace audio {
namespace synth {

using namespace chip_common;
using timing::SequencerTime;

// https://wiki.nesdev.com/w/index.php/CPU
// >Emulator authors may wish to emulate the NTSC NES/Famicom CPU at 21441960 Hz...
// >to ensure a synchronised/stable 60 frames per second.[2]
// int const MASTER_CLK_PER_S = 21'441'960;

// Except 2A03 APU operates off CPU clock (master clock / 12 if NTSC).
// https://wiki.nesdev.com/w/index.php/FDS_audio
// FDS also operates off CPU clock, despite 0CC storing master clock in a constant.
int constexpr CLOCKS_PER_S = 1'786'830;

// NTSC is approximately 60 fps.
int constexpr TICKS_PER_S = 60;


/// This type is used widely, so import to audio::synth.
using event_queue::ClockT;

using SampleT = uint32_t;

using Address = uint16_t;
using Byte = uint8_t;

struct RegisterWrite {
    Address address;
    Byte value;
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
    // I think music_driver::TimeRef will make it easier to use.
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
        assert(out.time_before == 0);
        return out.write;
    }

    size_t num_unread() {
        return vec.size() - output.index;
    }
};

// /// Each ChanToPatternData is logically tied to a ChipInstance,
// /// and has length == enum_count<ChipInstance::type-erased ChipKind>.

/// Static polymorphic properties of classes,
/// which can be accessed via pointers to subclasses.
#define STATIC_DECL(type_name_parens) \
    virtual type_name_parens const = 0;

#define STATIC(type_name_parens, value) \
    type_name_parens const final { return value; }

/// Base class, for a single NES chip's (software driver + sequencers
/// + hardware emulator synth).
/// Non-NES consoles may use a different base class (SNES) or maybe not (wavetable chips).
class ChipInstance : boost::noncopyable {
public:
    // fields
    RegisterWriteQueue _register_writes;

    // type-erased dependent values:
    //  defined in subclasses, enforced via runtime release_assert.
    //  This is OK because it's not checked in a hot inner loop.
    // type ChannelID = Apu1ChannelID;

    // impl
    virtual ~ChipInstance() = default;

    /// Ticks sequencer and buffers up events for a subsequent call to driver_tick().
    /// Sequencer's time passes.
    ///
    /// Return: SequencerTime is current tick (just occurred), not next tick.
    virtual SequencerTime sequencer_tick(doc::Document const & document) = 0;

    /// Mutates _register_writes.
    virtual void driver_tick(doc::Document const & document) = 0;

    /// Cannot cross tick boundaries. Can cross register-write boundaries.
    void run_chip_for(
        ClockT const prev_to_tick,
        ClockT const prev_to_next,
        Blip_Buffer & nes_blip,
        gsl::span<Amplitude> temp_buffer
    );

private:
    /// Called with data from _register_writes.
    /// Time does not pass.
    virtual void synth_write_memory(RegisterWrite write) = 0;

protected:
    /// If the synth generates audio via Blip_Synth, nsamp_returned == 0.
    /// If the synth writes audio into `write_buffer`,
    /// nsamp_returned == how many samples were written.
    using NsampWritten = SampleT;

private:
    /// Cannot cross tick boundaries, nor register-write boundaries.
    ///
    /// Most NesChipSynth subclasses will write to a Blip_Buffer
    /// (if they have a Blip_Synth with a mutable aliased reference to Blip_Buffer).
    /// The VRC7 will write to a Blip_Synth at high frequency (like Mesen).
    /// The FDS will instead write lowpassed audio to write_buffer.
    virtual NsampWritten synth_run_clocks(
        ClockT clk_begin, ClockT nclk, gsl::span<Amplitude> write_buffer
    ) = 0;
};

// end namespaces
}
}
