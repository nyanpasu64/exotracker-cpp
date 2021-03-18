#pragma once

#include "synth/music_driver_common.h"
#include "event_queue.h"
#include "audio_common.h"
#include "doc.h"
#include "chip_common.h"
#include "timing_common.h"
#include "util/enum_map.h"
#include "util/copy_move.h"
#include "util/release_assert.h"

#include <gsl/span>

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

using NsampT = uint32_t;

/// Static polymorphic properties of classes,
/// which can be accessed via pointers to subclasses.
#define STATIC_DECL(type_name_parens) \
    virtual type_name_parens const = 0;

#define STATIC(type_name_parens, value) \
    type_name_parens const final { return value; }

struct WriteTo {
    gsl::span<Amplitude> out;
};

/// Base class, for a single NES chip's (software driver + sequencers
/// + hardware emulator synth).
/// Non-NES consoles may use a different base class (SNES) or maybe not (wavetable chips).
class ChipInstance {
    // Some implementations are self-referential.
    DISABLE_COPY_MOVE(ChipInstance)

// fields
protected:
    /// One register write queue per chip.
    RegisterWriteQueue _register_writes;

// impl
public:
    explicit ChipInstance() = default;

    virtual ~ChipInstance() = default;

    /// Seek the sequencer to this time in the document (grid cell and beat fraction).
    /// ChipInstance does not know if the song/sequencer is playing or not.
    /// OverallSynth is responsible for calling sequencer_driver_tick() during playback,
    /// and stop_playback() once followed by driver_tick() when not playing.
    virtual void seek(doc::Document const & document, timing::GridAndBeat time) = 0;

    /// Similar to seek(), but ignores events entirely (only looks at tempo/rounding).
    /// Keeps position in event list, recomputes real time in ticks.
    /// Can be called before doc_edited() if both tempo and events edited.
    virtual void tempo_changed(doc::Document const & document) = 0;

    /// Assumes tempo is unchanged, but events are changed.
    /// Keeps real time in ticks, recomputes position in event list.
    virtual void doc_edited(doc::Document const & document) = 0;

    /// Called when the timeline rows are edited.
    /// The cursor may no longer be in-bounds, so clamp the cursor to be in-bounds.
    /// Rows may be added, deleted, or change duration,
    /// so invalidate both real time and events.
    virtual void timeline_modified(doc::Document const & document) = 0;

    /// Call on each tick, before calling any functions which run the driver
    /// (stop_playback() or [sequencer_]driver_tick()).
    void flush_register_writes();

    /// May/not mutate _register_writes.
    /// You are required to call driver_tick() afterwards on the same tick,
    /// or notes may not necessarily stop.
    virtual void stop_playback() = 0;

    /// Ticks sequencer and runs driver.
    ///
    /// Return: SequencerTime is current tick (just occurred), not next tick.
    virtual SequencerTime sequencer_driver_tick(doc::Document const & document) = 0;

    /// Runs driver, ignoring sequencer. Called when the song is not playing.
    virtual void driver_tick(doc::Document const & document) = 0;

    /// Cannot cross tick boundaries. Can cross register-write boundaries.
    void run_chip_for(
        ClockT const prev_to_tick,
        ClockT const prev_to_next,
        WriteTo write_to);

private:
    /// Called with data from _register_writes.
    /// Time does not pass.
    virtual void synth_write_memory(RegisterWrite write) = 0;

public:
    /// If the synth generates audio via Blip_Synth, nsamp_returned == 0.
    /// If the synth writes audio into `write_buffer`,
    /// nsamp_returned == how many samples were written.
    using NsampWritten = NsampT;

private:
    /// Cannot cross tick boundaries, nor register-write boundaries.
    ///
    /// Most NesChipSynth subclasses will write to a Blip_Buffer
    /// (if they have a Blip_Synth with a mutable aliased reference to Blip_Buffer).
    /// The VRC7 will write to a Blip_Synth at high frequency (like Mesen).
    /// The FDS will instead write lowpassed audio to write_buffer.
    virtual NsampWritten synth_run_clocks(
        ClockT clk_begin,
        ClockT nclk,
        WriteTo write_to) = 0;
};

// end namespaces
}
}
