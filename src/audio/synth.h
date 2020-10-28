#pragma once

#include "synth/nes_2a03.h"
#include "synth_common.h"
#include "audio_common.h"
#include "callback.h"
#include "doc.h"
#include "timing_common.h"
#include "cmd_queue.h"
#include "util/enum_map.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace audio {
namespace synth {

/// States for the synth callback loop.
enum class SynthEvent {
    // EndOfCallback comes before Tick.
    // If a callback ends at the same time as a tick occurs,
    // the tick should happen next callback.
    EndOfCallback,
    Tick,
    COUNT
};

using cmd_queue::AudioCommand;
using timing::MaybeSequencerTime;

/// Preconditions:
/// - Sampling rate must be 1000 or more.
///   Otherwise blip_buffer constructor/set_sample_rate() breaks.
/// - Tick rate must be over 4 tick/second.
///   Otherwise blip_buffer count_clocks(nsamp) breaks.
/// - samp/s / ticks/sec < 65536. Otherwise _temp_buffer overflows.
class OverallSynth : boost::noncopyable, public callback::CallbackInterface {
    // runtime constants
public:
    uint32_t const _stereo_nchan;

private:
    doc::Document _document;

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
    ClockT const _clocks_per_tick = CLOCKS_PER_S / TICKS_PER_S;

    /// Must be 1 or greater.
    /// Increasing it past 1 causes compatible sound synths to only be sampled
    /// (sent to Blip_Buffer) every n clocks.
    ///
    /// Not all sound synths may actually take this as a parameter.
    /// In particular, N163 and VRC7 use time-division mixing,
    /// which has high-frequency content and may produce lots of aliasing if downsampled.
    ClockT const _clocks_per_sound_update;

    // fields
    EventQueue<SynthEvent> _events;

    // Audio written into this and read to output.
    Blip_Buffer _nes_blip;

    /// vector<ChipIndex -> unique_ptr<ChipInstance subclass>>
    /// _chip_instances.size() in [1..MAX_NCHIP] inclusive. Derived from Document::chips.
    std::vector<std::unique_ptr<ChipInstance>> _chip_instances = {};

    // Playback tracking
    /// Last seen/processed command.
    std::atomic<AudioCommand *> _seen_command;
    bool _sequencer_running = false;

    using AtomicSequencerTime = std::atomic<MaybeSequencerTime>;
    static_assert(
        AtomicSequencerTime::is_always_lock_free,
        "std::atomic<MaybeSequencerTime> not lock-free"
    );

    AtomicSequencerTime _maybe_seq_time{MaybeSequencerTime{}};

    /// Per-chip "special audio" written into this and read into _nes_blip.
    /// This MUST remain the last field in the struct,
    /// which may/not improve memory locality of other fields.
    Amplitude _temp_buffer[1 << 16];

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

private:
    // blip_buffer uses blip_long AKA signed int for nsamp.
    using SampleT = blip_nsamp_t;

public:
    /// Generates audio to be sent to an audio output (speaker) or WAV file.
    /// The entire output buffer is written to.
    ///
    /// output must have length length of mono_smp_per_block * stereo_nchan.
    /// It is treated as an array of interleaved samples, [smp#, * nchan + chan#] Amplitude.
    void synthesize_overall(
        gsl::span<Amplitude> output_buffer,
        size_t const mono_smp_per_block
    );

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
