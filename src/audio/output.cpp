#include "output.h"

#include "synth.h"

#include <gsl/span>
#include <gsl/gsl_algorithm>
#include <cstdint>  // int16_t
#include <type_traits>  // is_same_v

namespace audio {
namespace output {

// When changing the output sample format,
// be sure to change Amplitude (audio_common.h) and AmplitudeFmt at the same time!
static_assert(std::is_same_v<Amplitude, int16_t>);
const RtAudioFormat AmplitudeFmt = RTAUDIO_SINT16;

static unsigned int const STEREO_NCHAN = 2;


/// GUI-only audio synthesis callback.
/// Uses GetDocument to obtain a document reference every callback.
/// The document (possibly address too) will change when the user edits the document.
class OutputCallback : public CallbackInterface {
private:
    synth::OverallSynth synth;
    locked_doc::GetDocument &/*'a*/ _get_document;

public:
    static std::unique_ptr<OutputCallback> make(
        uint32_t stereo_nchan,
        uint32_t smp_per_s,
        locked_doc::GetDocument &/*'a*/ get_document,
        AudioOptions audio_options
    ) {
        // Outlives the return constructor call. Outlives all use of *doc_guard.
        auto doc_guard = get_document.get_document();
        return std::make_unique<OutputCallback>(
            stereo_nchan, smp_per_s, *doc_guard, audio_options, get_document
        );
    }

    OutputCallback(
        uint32_t stereo_nchan,
        uint32_t smp_per_s,
        doc::Document const & document,
        AudioOptions audio_options,
        locked_doc::GetDocument &/*'a*/ get_document
    ) :
        synth(synth::OverallSynth(stereo_nchan, smp_per_s, document, audio_options)),
        _get_document(get_document)
    {}

    // impl CallbackInterface

    MaybeSequencerTime play_time() const override {
        return synth.play_time();
    }

    // interleaved=true => outputBufferVoid: [smp#, * nchan + chan#] Amplitude
    // interleaved=false => outputBufferVoid: [chan#][smp#]Amplitude
    // interleaved=false was added to support ASIO's native representation.
    static constexpr RtAudioStreamFlags rtaudio_flags = RTAUDIO_NONINTERLEAVED;

    // impl RtAudioCallback
    static int rtaudio_callback(
        void *outputBufferVoid, void *inputBufferVoid,
        unsigned int mono_smp_per_block,
        double streamTime,
        RtAudioStreamStatus status,
        void *userData
    ) {
        auto self = static_cast<OutputCallback *>(userData);
        synth::OverallSynth & synth = self->synth;

        // Convert output buffer from raw pointer into GSL span.
        size_t stereo_smp_per_block = size_t(synth._stereo_nchan) * mono_smp_per_block;

        static_assert(
            (STEREO_NCHAN == 2) && (rtaudio_flags | RTAUDIO_NONINTERLEAVED),
            "rtaudio_callback() assumes planar stereo"
        );
        gsl::span output{(Amplitude *) outputBufferVoid, stereo_smp_per_block};
        auto left = output.subspan(0, mono_smp_per_block);
        auto right = output.subspan(mono_smp_per_block, mono_smp_per_block);

        {
            auto doc_guard = self->_get_document.get_document();
            synth.synthesize_overall(*doc_guard, left, mono_smp_per_block);
        }
        gsl::copy(left, right);

        return 0;
    }

};


static unsigned int const MONO_SMP_PER_BLOCK = 64;


/// Why factory method and not constructor?
/// So we can calculate values (like sampling rate) used in multiple places.
std::optional<AudioThreadHandle> AudioThreadHandle::make(
    RtAudio & rt, unsigned int device, locked_doc::GetDocument & get_document
) {
    RtAudio::StreamParameters outParams;
    outParams.deviceId = device;
    outParams.nChannels = STEREO_NCHAN;

    RtAudio::StreamOptions stream_opt;
    stream_opt.flags = OutputCallback::rtaudio_flags;

    unsigned int sample_rate = 48000;
    unsigned int mono_smp_per_block = MONO_SMP_PER_BLOCK;
    AudioOptions audio_options {
        .clocks_per_sound_update = 4,
    };

    std::unique_ptr<OutputCallback> callback = OutputCallback::make(
        outParams.nChannels, sample_rate, get_document, audio_options
    );

    // On OpenSUSE Tumbleweed, if you hold F12,
    // sometimes PulseAudio tells RtAudio there are 0 output devices,
    // and RtAudio throws an exception trying to open device 0.
    // TODO return RtAudio error message? They're not very useful TBH.
    try {
        rt.openStream(
            &/*mut*/ outParams,
            nullptr,
            AmplitudeFmt,
            sample_rate,
            &/*mut*/ mono_smp_per_block,
            OutputCallback::rtaudio_callback,
            callback.get(),
            &/*mut*/ stream_opt
        );
        /*
        What does RtAudio::openStream() mutate?

        outParams: Not mutated. If nChannels decreased, would result in out-of-bounds writes.
        mono_smp_per_block: Mutated by DirectSound, but callback doesn't store the old value.
        stream_opt: Only numberOfBuffers is mutated. If flags mutated, would result in garbled audio.
        */

        rt.startStream();
    } catch (RtAudioError & e) {
        e.printMessage();
        return {};
    }

    return {AudioThreadHandle{rt, std::move(callback)}};
}

AudioThreadHandle::~AudioThreadHandle() {
    // Don't stop audio if this has been moved from.
    if (!_callback) {
        return;
    }

    try {
        _rt.get().stopStream();
    } catch (RtAudioError & e) {
        e.printMessage();
    }

    _rt.get().closeStream();
}

// end namespaces
}
}
