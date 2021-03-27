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

/// This type is used widely, so import to audio::synth.
using event_queue::ClockT;

using NsampT = uint32_t;

/// Nominal sampling rate, used when computing tuning tables and tempos.
/// The user changing the emulated sampling rate rate (and clock rate)
/// should not affect how the driver computes pitches and timers,
/// since that would introduce a source of behavioral discrepancies.
constexpr NsampT SAMPLES_PER_S_IDEAL = 32040;

constexpr uint32_t STEREO_NCHAN = 2;

/// Static polymorphic properties of classes,
/// which can be accessed via pointers to subclasses.
#define STATIC_DECL(type_name_parens) \
    virtual type_name_parens const = 0;

#define STATIC(type_name_parens, value) \
    type_name_parens const final { return value; }

/// A sample of digital audio, directly from the S-DSP.
using SpcAmplitude = int16_t;

/// Unfiltered non-oversampled digital audio, directly from the S-DSP.
using WriteTo = gsl::span<SpcAmplitude>;

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

    // # Sequencer methods.

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

    // # Driver methods

    /// Must be called upon construction, or when samples change.
    /// Repack all samples into RAM, and stops all running notes
    /// (which would be playing at the wrong point).
    ///
    /// TODO only stop samples being played, and remap addresses of running samples
    /// (construct a mapping table using additional sample allocation/mapping metadata).
    virtual void reload_samples(doc::Document const & document) = 0;

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

    // # Base class methods

    /// Call on each tick, before calling any functions which run the driver
    /// (stop_playback() or [sequencer_]driver_tick()).
    void flush_register_writes();

    using NsampWritten = NsampT;

    /// Run the chip for 1 tick, applying register writes and generating audio.
    /// Can cross register-write boundaries.
    /// Calls synth_run_clocks() once per register write.
    NsampWritten run_chip_for(
        ClockT const num_clocks,
        WriteTo write_to);

private:
    /// Called by run_chip_for() with data from _register_writes.
    /// Time does not pass.
    virtual void synth_write_memory(RegisterWrite write) = 0;

    /// Called by run_chip_for() once per register write.
    /// Cannot cross register-write boundaries.
    virtual NsampWritten synth_run_clocks(
        ClockT nclk,
        WriteTo write_to) = 0;
};

// end namespaces
}
}
