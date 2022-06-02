#include "output.h"

#include "callback.h"
#include "synth.h"

#include <gsl/span>
#include <gsl/gsl_algorithm>
#include <cstdint>  // int16_t
#include <type_traits>  // is_same_v

#include <fmt/core.h>

namespace audio {
namespace output {

// When changing the output sample format,
// be sure to change Amplitude (audio_common.h) and AmplitudeFmt at the same time!
static_assert(std::is_same_v<Amplitude, float>);
static constexpr RtAudioFormat AmplitudeFmt = RTAUDIO_FLOAT32;

using synth::STEREO_NCHAN;
using synth::OverallSynth;

// interleaved=true => outputBufferVoid: [smp#, * nchan + chan#] Amplitude
// interleaved=false => outputBufferVoid: [chan#][smp#]Amplitude
// interleaved=false was added to support ASIO's native representation.

// In JACK Audio mode, jackd sets our thread to real-time.
// In ALSA/etc., RtAudio handles modes.
static constexpr RtAudioStreamFlags RTAUDIO_FLAGS = RTAUDIO_SCHEDULE_REALTIME;

// impl RtAudioCallback
static int rtaudio_callback(
    void * output_buffer,
    void * input_buffer,
    unsigned int mono_smp_per_block,
    double stream_time,
    RtAudioStreamStatus status,
    void * synth_void
) {
    auto & synth = *static_cast<OverallSynth *>(synth_void);

    // Convert output buffer from raw pointer into GSL span.
    size_t stereo_smp_per_block = size_t(STEREO_NCHAN) * mono_smp_per_block;

    static_assert(
        (STEREO_NCHAN == 2) && !(RTAUDIO_FLAGS & RTAUDIO_NONINTERLEAVED),
        "rtaudio_callback() assumes interleaved stereo"
    );
    gsl::span output{(Amplitude *) output_buffer, stereo_smp_per_block};
    synth.synthesize_overall(output, mono_smp_per_block);

    return 0;
}

// Any lower latency, and I get audio dropouts on PulseAudio.
// Not sure if it's because of PulseAudio, exotracker, non-real-time threads,
// or Linux kernel.
static constexpr unsigned int MONO_SMP_PER_BLOCK = 512;
static constexpr unsigned int NUM_BLOCKS = 2;

/// Why factory method and not constructor?
/// So we can calculate values (like sampling rate) used in multiple places.
std::optional<AudioThreadHandle> AudioThreadHandle::make(
    RtAudio & rt,
    unsigned int device,
    doc::Document document,
    AudioCommand * stub_command
) {
    RtAudio::StreamParameters outParams;
    outParams.deviceId = device;
    outParams.nChannels = STEREO_NCHAN;

    RtAudio::StreamOptions stream_opt;
    stream_opt.numberOfBuffers = NUM_BLOCKS;
    stream_opt.flags = RTAUDIO_FLAGS;

    unsigned int sample_rate = 48000;
    unsigned int mono_smp_per_block = MONO_SMP_PER_BLOCK;
    AudioOptions audio_options {
    };

    auto synth = std::make_unique<OverallSynth>(
        outParams.nChannels, sample_rate, std::move(document), stub_command, audio_options
    );

    // On OpenSUSE Tumbleweed, if you hold F12,
    // sometimes PulseAudio tells RtAudio there are 0 output devices,
    // and RtAudio throws an exception trying to open device 0.
    // TODO return RtAudio error message? They're not very useful TBH.
    auto e = rt.openStream(
            &/*mut*/ outParams,
            nullptr,
            AmplitudeFmt,
            sample_rate,
            &/*mut*/ mono_smp_per_block,
            rtaudio_callback,
            synth.get(),
            &/*mut*/ stream_opt
        );
    if (e != RTAUDIO_NO_ERROR) {
        fmt::print("RtAudio::openStream() error {}\n", e);
        return {};
    }
    /*
    What does RtAudio::openStream() mutate?

    outParams: Not mutated. If nChannels decreased, would result in out-of-bounds writes.
    mono_smp_per_block: Mutated by DirectSound, but callback doesn't store the old value.
    stream_opt: Only numberOfBuffers is mutated. If flags mutated, would result in garbled audio.
    */

    fmt::print(stderr,
        "{} smp/block, {} buffers\n", mono_smp_per_block, stream_opt.numberOfBuffers
    );

    e = rt.startStream();
    if (e != RTAUDIO_NO_ERROR) {
        fmt::print("RtAudio::startStream() error {}\n", e);
        return {};
    }

    return {AudioThreadHandle{rt, std::move(synth)}};
}

AudioThreadHandle::~AudioThreadHandle() {
    // Don't stop audio if this has been moved from.
    if (!_callback) {
        return;
    }

    auto e = _rt.get().stopStream();
    if (e != RTAUDIO_NO_ERROR) {
        fmt::print("RtAudio::stopStream() error {}\n", e);
    }

    _rt.get().closeStream();
}

// end namespaces
}
}
