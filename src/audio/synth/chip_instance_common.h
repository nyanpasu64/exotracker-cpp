#pragma once

#include "../synth_common.h"
#include "music_driver_common.h"
#include "audio/event_queue.h"
#include "audio/audio_common.h"
#include "audio/tempo_calc.h"
#include "doc.h"
#include "chip_common.h"
#include "timing_common.h"
#include "util/enum_map.h"
#include "util/copy_move.h"

#include <gsl/span>

#include <vector>

namespace audio::synth::chip_instance {

using namespace chip_common;
using timing::SequencerTime;
using music_driver::RegisterWrite;
using music_driver::RegisterWriteQueue;

using audio::tempo_calc::SAMPLES_PER_S_IDEAL;

/// Base class (interface-ish) for a single SPC-700's (software driver + sequencers
/// + hardware emulator synth).
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

// # Playback control methods

    /// Seek the sequencer to this time in the document (grid cell and beat fraction).
    /// ChipInstance does not know if the song/sequencer is playing or not.
    /// OverallSynth is responsible for calling sequencer_driver_tick() during playback,
    /// and stop_playback() once followed by driver_tick() when not playing.
    virtual void seek(doc::Document const& document, timing::GridAndBeat time) = 0;

    /// Stop the sequencer, and tell the driver to stop all playing notes.
    /// May/not mutate _register_writes.
    /// You are required to call driver_tick() afterwards on the same tick,
    /// or notes may not necessarily stop.
    virtual void stop_playback() = 0;

// # Sequencer-mutation methods. Ignored when the sequencer is stopped.
// The sequencer starts out stopped, begins playing when seek() is called,
// and stops playing when stop_playback() is called.

    /// Similar to seek(), but ignores events entirely (only looks at tempo/rounding).
    /// Keeps position in event list, recomputes real time in ticks.
    /// Can be called before doc_edited() if both tempo and events edited.
    virtual void ticks_per_beat_changed(doc::Document const& document) = 0;

    /// Assumes tempo is unchanged, but events are changed.
    /// Keeps real time in ticks, recomputes position in event list.
    virtual void doc_edited(doc::Document const& document) = 0;

    /// Called when the timeline rows are edited.
    /// The cursor may no longer be in-bounds, so clamp the cursor to be in-bounds.
    /// Rows may be added, deleted, or change duration,
    /// so invalidate both real time and events.
    virtual void timeline_modified(doc::Document const& document) = 0;

// # Driver methods

    /// Reset driver and synth state. Called whenever playback begins.
    /// You are required to call driver_tick() afterwards on the same tick.
    virtual void reset_state(doc::Document const& document) = 0;

    /// Must be called upon construction, or when samples change.
    /// Repack all samples into RAM, and stops all running notes
    /// (which would be playing at the wrong point).
    ///
    /// TODO only stop samples being played, and remap addresses of running samples
    /// (construct a mapping table using additional sample allocation/mapping metadata).
    virtual void reload_samples(doc::Document const& document) = 0;

// # Tick methods. On every SNES timer, call exactly 1 of these,
// # followed by run_chip_for().

    /// Run the sequencer to obtain a list of events, then pass them to the driver.
    /// Tell the driver that a sequencer tick has occurred.
    /// This triggers events (notes) and advances both real-time and tempo-driven effects.
    ///
    /// This method is only called when the song is playing.
    /// The rate of it being called is proportional to the current tempo.
    ///
    /// Return: SequencerTime is current tick (just occurred), not next tick.
    virtual SequencerTime tick_sequencer(doc::Document const& document) = 0;

    /// Don't advance the sequencer, and pass the driver an empty list of events.
    /// Tell it to advance real-time but not tempo-driven effects.
    ///
    /// This method is called both when the song is playing and stopped.
    /// When playing, it is called whenever the SNES timer advances
    /// but a sequencer tick is not triggered.
    /// When stopped, this is called on every timer.
    virtual void run_driver(doc::Document const& document) = 0;

// # Base class methods

    /// Run the chip for 1 tick, applying register writes and generating audio.
    /// Can cross register-write boundaries.
    /// Calls synth_write_reg() once per register write,
    /// and synth_run_clocks() in between to advance time.
    NsampWritten run_chip_for(ClockT const num_clocks, WriteTo write_to);

    /// Call at the end of each tick.
    void flush_register_writes();

// # Implemented by subclasses, called by base class.
private:
    /// Called by run_chip_for() with data from _register_writes.
    /// Time does not pass.
    virtual void synth_write_reg(RegisterWrite write) = 0;

    /// Called by run_chip_for() in between register writes.
    /// Time passes.
    virtual NsampWritten synth_run_clocks(
        ClockT nclk,
        WriteTo write_to) = 0;
};

// end namespaces
}
