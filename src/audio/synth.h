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
    bool _sequencer_running = false;

    /// If _document.sequencer_options.use_exact_tempo is false,
    /// must be a multiple of CLOCKS_PER_PHASE (128).
    ClockT _clocks_per_tick;

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
