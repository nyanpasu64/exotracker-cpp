#pragma once

#include "synth/chip_instance_common.h"
#include "audio_common.h"
#include "callback.h"
#include "doc.h"
#include "timing_common.h"
#include "cmd_queue.h"
#include "util/enum_map.h"
#include "util/copy_move.h"

#include <samplerate.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>

namespace audio {
namespace synth {

using chip_instance::ChipInstance;
using cmd_queue::AudioCommand;
using timing::MaybeSequencerTime;

class SpcResampler {
    SRC_STATE * _resampler = nullptr;
    gsl::span<float> _input;  // Points into OverallSynth's members.

    uint32_t _stereo_nchan;
    double _output_smp_per_s;
    SRC_DATA _resampler_args = {};

public:
    SpcResampler(
        uint32_t stereo_nchan, uint32_t smp_per_s, AudioOptions const& audio_options
    );
    ~SpcResampler();

    template<typename Fn>
    void resample(Fn generate_input, gsl::span<float> out);
};

enum class TimerEvent {
    /// See ChipInstance::tick_sequencer() doc comment.
    TickSequencer = 1,
    /// See ChipInstance::run_driver() doc comment.
    RunDriver = 0,
};

/**
exotracker's timing system uses tempo (a phase accumulator ticked by a fixed timer
and incrementing by a variable amount each time, triggering tempo ticks upon overflow)
because it maps linearly to BPM, has decent precision, and has a fairly approachable UI.
By contrast, it's difficult to expose a simple/intuitive UI for speed/groove,
especially if you want the user to input BPM values rather than periods/speeds.

Additionally, tempo's fixed timer allows specifying instrument/effect durations
and delays (though not lookaheads) in real time,
which is not possible with variable timers.

The problem is that tempo causes nondeterministic timing jitter for tempo-based events,
which is likely worse for short notes. Given a 205 Hz timer (like FF6),
the jitter is around 5 ms, which is generally not noticeable to listeners.
Jitter is absent from variable-timer tempo systems (reasonably fine-grained tempo)
and FamiTracker's speed (coarse-grained tempo), and present (but identical each beat)
in exotracker's "ticks/beat" and FamiTracker's groove.

----

Does SequencerTiming need to keep sending tempo ticks to the driver
when the song/sequencer are stopped, using a fake tempo? (no.)

I don't like sending tempo ticks when stopped.
It's a bad idea to make previewing instruments rely on tempo ticks to behave properly.
What tempo would you pick when you stop playback?

- Reload the timing system with the global/initial tempo.
    - OK: vibrato preview is no longer accurate if tempo changes midway through.
- Keep the most recent tempo.
    - Bad: hidden state, nondeterministic preview.
- Use the tempo of the cursor.
    - Difficult to implement. This needs a comprehensive "recall channel state",
      and it must be called (on the audio thread?) whenever the cursor moves.
    - Vibrato preview sounds different when you move the cursor.
    - This is indicative of a deeper flaw: instruments are not deterministic,
      but depend on global mutable state (current tempo/most recent tempo change).

What driver features would break if we don't send tempo ticks when stopped?

- We add instrument parameters or pattern effects (like delayed vibrato)
  with durations measured in tempo ticks.
- They control driver behavior when previewing instruments without a song playing.
  (Crescendos don't count, they only trigger during playback.)

So the driver must be written so that whatever time-dependent effects I use
as instrument parameters (delayed vibrato, maybe stacato)
are measured in timers, not tempo ticks.

----

Sometimes the document tempo changes, whether from the user editing settings,
or from mid-song effects changing the phase step. We need to react to it.
it's easiest to bundle up all tempo-related state into one object (SequencerTiming)
and centralize tempo-changing logic into recompute_tempo().

----

OverallSynth::synthesize_tick_oversampled()
generates one "emulated SMP timer duration" of audio at a time.

It calls SequencerTiming::clocks_per_timer() to obtain a duration,
then clocks the S-DSP emulator by that many clocks to generate samples.

It calls SequencerTiming::run_timer() to determine whether to send a tempo tick or not.
*/
class SequencerTiming {
// Fields
    /// Must be a multiple of CLOCKS_PER_PHASE (128).
    ClockT _clocks_per_timer;

    /// How much to advance the sequencer phase by, on each step.
    /// When the phase overflows, a sequencer tick is triggered.
    uint8_t _phase_step;

    bool _running = false;
    uint8_t _phase;

// Constants
    /// Initializing to 0xff is unusual,
    /// but it's the easiest way to tick the sequencer immediately when playback begins.
    /// I'm too lazy to add a separate boolean flag.
    constexpr static uint8_t DEFAULT_SEQUENCER_PHASE = 0xff;

public:
    SequencerTiming(doc::SequencerOptions const& options);

    void recompute_tempo(doc::SequencerOptions const& options);

    // TODO handle mid-song tempo changes not reflected in SequencerOptions.

    ClockT clocks_per_timer() const {
        return _clocks_per_timer;
    }

    // TODO bool song_playing() const;

    /// Begin playback, reset the phase, etc.
    void play();
    void stop();

    /// Called once per emulated SNES timer.
    /// Increments the phase accumulator if the song is playing.
    ///
    /// The return value specifies whether to run the driver only
    /// (with a non-tempo tick),
    /// or tick the sequencer and send the driver a tempo tick.
    /// No tempo ticks are sent to either the sequencer or driver
    /// when the song is not playing.
    TimerEvent run_timer();
};

/// Preconditions: TODO???
class OverallSynth : public callback::CallbackInterface {
    DISABLE_COPY_MOVE(OverallSynth)

private:
    doc::Document _document;
    AudioOptions _audio_options;

    // fields
    SpcResampler _resampler;
    std::vector<SpcAmplitude> _temp_buf;
    std::vector<float> _resampler_input;

    /// vector<ChipIndex -> unique_ptr<ChipInstance subclass>>
    /// _chip_instances.size() in [1..MAX_NCHIP] inclusive. Derived from Document::chips.
    std::vector<std::unique_ptr<ChipInstance>> _chip_instances = {};

    // Playback tracking
    /// Last seen/processed command.
    std::atomic<AudioCommand *> _seen_command;

    SequencerTiming _sequencer_timing;

    using AtomicSequencerTime = std::atomic<MaybeSequencerTime>;
    static_assert(
        AtomicSequencerTime::is_always_lock_free,
        "std::atomic<MaybeSequencerTime> not lock-free"
    );

    AtomicSequencerTime _maybe_seq_time{MaybeSequencerTime{}};

public:
    // impl
    /// Preconditions:
    /// - get_document argument must outlive returned OverallSynth.
    /// - get_document's list of chips must not change between calls.
    ///   If it changes, discard returned OverallSynth and create a new one.
    OverallSynth(
        uint32_t stereo_nchan,
        uint32_t smp_per_s,
        doc::Document document,
        AudioCommand * stub_command,
        AudioOptions audio_options
    );

    /// Generates audio to be sent to an audio output (speaker) or WAV file.
    /// The entire output buffer is written to.
    ///
    /// output must have length length of mono_smp_per_block * stereo_nchan.
    /// It is treated as an array of interleaved samples, [smp#, * nchan + chan#] Amplitude.
    ///
    /// This function only performs resampling; the actual synthesis is in synthesize_tick_oversampled().
    void synthesize_overall(
        gsl::span<Amplitude> output_buffer,
        size_t const mono_smp_per_block
    );

private:
    /// lmfao
    gsl::span<float> synthesize_tick_oversampled();

public:
    /// Called by GUI thread.
    AudioCommand * seen_command() const override {
        // Paired with synthesize_overall() store(release).
        return _seen_command.load(std::memory_order_acquire);
    }

    /// Called by GUI thread.
    MaybeSequencerTime play_time() const override {
        return _maybe_seq_time.load(std::memory_order_seq_cst);
    }
};


// end namespaces
}
}
