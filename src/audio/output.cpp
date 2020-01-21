#include "output.h"

#include "synth.h"

#include <gsl/span>
#include <cstdint>  // int16_t
#include <type_traits>  // is_same_v

namespace audio {
namespace output {

/// GUI-only audio synthesis callback.
/// Uses GetDocument to obtain a document reference every callback.
/// The document (possibly address too) will change when the user edits the document.
class OutputCallback : public pa::CallbackInterface, private synth::OverallSynth {
private:
    doc::GetDocument &/*'a*/ _get_document;

public:
    static std::unique_ptr<OutputCallback> make(
        int stereo_nchan,
        int smp_per_s,
        doc::GetDocument &/*'a*/ get_document,
        AudioOptions audio_options
    ) {
        // Outlives the return constructor call. Outlives all use of *doc_guard.
        auto & document = get_document.get_document();
        return std::make_unique<OutputCallback>(
            stereo_nchan, smp_per_s, document, audio_options, get_document
        );
    }

    OutputCallback(
        int stereo_nchan,
        int smp_per_s,
        doc::Document const & document,
        AudioOptions audio_options,
        doc::GetDocument &/*'a*/ get_document
    ) :
        synth::OverallSynth(stereo_nchan, smp_per_s, document, audio_options),
        _get_document(get_document)
    {}

    // interleaved=true => outputBufferVoid: [smp#, * nchan + chan#] Amplitude
    // interleaved=false => outputBufferVoid: [chan#][smp#]Amplitude
    // interleaved=false was added to support ASIO's native representation.
    static constexpr bool interleaved = true;

    // impl pa::CallbackInterface

    // returns PaStreamCallbackResult.
    int paCallbackFun(const void *inputBufferVoid, void *outputBufferVoid, unsigned long mono_smp_per_block,
                      const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags) override {
        synth::OverallSynth & synth = *this;

        // Convert output buffer from raw pointer into GSL span.
        std::ptrdiff_t stereo_smp_per_block = synth._stereo_nchan * mono_smp_per_block;
        gsl::span output{(Amplitude *) outputBufferVoid, stereo_smp_per_block};

        auto & document = _get_document.get_document();
        synth.synthesize_overall(document, output, mono_smp_per_block);

        return PaStreamCallbackResult::paContinue;
    }

};


/// Stream which stops and closes itself when destroyed.
///
/// Many classes inherit from pa::CallbackStream. I picked one I like.
/// InterfaceCallbackStream and MemFunCallbackStream all take function pointers,
/// there's no static dispatch.
class SelfTerminatingStream : public pa::InterfaceCallbackStream {
public:
    using pa::InterfaceCallbackStream::InterfaceCallbackStream;

    ~SelfTerminatingStream() override {
        // Mimics portaudio/bindings/cpp/examples/sine.cxx and rust portaudio `Drop for Stream`.
        try {
            this->stop();
        } catch (...) {}
        try {
            this->close();
        } catch (...) {}
    }
};


// When changing the output sample format,
// be sure to change Amplitude (audio_common.h) and AmplitudeFmt at the same time!
static_assert(std::is_same_v<Amplitude, int16_t>);
const auto AmplitudeFmt = portaudio::SampleDataFormat::INT16;

static int const STEREO_NCHAN = 1;
static uintptr_t const MONO_SMP_PER_BLOCK = 64;


/// Why factory method and not constructor?
/// So we can calculate values (like sampling rate) used in multiple places.
AudioThreadHandle AudioThreadHandle::make(
    portaudio::System & sys, doc::GetDocument & get_document
) {
    portaudio::DirectionSpecificStreamParameters outParams(
        sys.defaultOutputDevice(),
        STEREO_NCHAN,
        AmplitudeFmt,
        output::OutputCallback::interleaved,
        sys.defaultOutputDevice().defaultLowOutputLatency(),
        nullptr
    );
    portaudio::StreamParameters params(
        portaudio::DirectionSpecificStreamParameters::null(),
        outParams,
        48000.0,
        (unsigned long) MONO_SMP_PER_BLOCK,
        paNoFlag
    );
    AudioOptions audio_options {
        .clocks_per_sound_update = 4,
    };

    // We cannot move/memcpy due to self-reference (stream holds reference to callback).
    // C++17 guaranteed copy elision only works on prvalues, not locals.
    // So let constructor initialize fields in-place (our factory method cannot).
    return AudioThreadHandle{outParams, params, get_document, audio_options};
}

AudioThreadHandle::AudioThreadHandle(
    portaudio::DirectionSpecificStreamParameters outParams,
    portaudio::StreamParameters params,
    doc::GetDocument & get_document,
    AudioOptions audio_options
) :
    callback(OutputCallback::make(
        outParams.numChannels(), (int)params.sampleRate(), get_document, audio_options
    )),
    stream(std::make_unique<SelfTerminatingStream>(params, *callback))
{
    stream->start();
}

// end namespaces
}
}
