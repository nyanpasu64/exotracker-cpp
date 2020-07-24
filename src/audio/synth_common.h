#pragma once

#include "make_blip_buffer.h"
#include "synth/music_driver_common.h"
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
using music_driver::RegisterWrite;
using music_driver::RegisterWriteQueue;

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

    /// Seek the sequencer to this time in the document
    /// (sequence entry and beat fraction).
    virtual void seek(doc::Document const & document, timing::PatternAndBeat time) = 0;

    /// May/not mutate _register_writes.
    /// You are required to call driver_tick() afterwards on the same tick,
    /// or notes may not necessarily stop.
    virtual void stop_playback() = 0;

    /// Assumes tempo is unchanged, but events are changed.
    /// Keeps real time in ticks, recomputes position in event list.
    virtual void doc_edited(doc::Document const & document) = 0;

    /// Similar to seek(), but ignores events entirely (only looks at tempo/rounding).
    /// Keeps position in event list, recomputes real time in ticks.
    /// Can be called before doc_edited() if both tempo and events edited.
    virtual void tempo_changed(doc::Document const & document) = 0;

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
