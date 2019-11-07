#include "output.h"

#include "synth.h"
#include "audio_common.h"

#include <gsl/gsl>
#include <cstdint>  // int16_t
#include <type_traits>  // is_same_v

namespace audio {
namespace output {

/// TODO rename OutputCallback to PortAudioCallback.
class OutputCallback : public pa::CallbackInterface, private synth::OverallSynth {

public:
    // interleaved=true => outputBufferVoid: [smp#, * nchan + chan#] Amplitude
    // interleaved=false => outputBufferVoid: [chan#][smp#]Amplitude
    // interleaved=false was added to support ASIO's native representation.
    static const bool interleaved = true;

    using synth::OverallSynth::OverallSynth;

    // impl pa::CallbackInterface

    // returns PaStreamCallbackResult.
    int paCallbackFun(const void *inputBufferVoid, void *outputBufferVoid, unsigned long mono_smp_per_block,
                      const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags) override {
        synth::OverallSynth & synth = *this;

        // Convert output buffer from raw pointer into GSL span.
        std::ptrdiff_t stereo_smp_per_block = synth._stereo_nchan * mono_smp_per_block;
        gsl::span output{(Amplitude *) outputBufferVoid, stereo_smp_per_block};

        synth.synthesize_overall(output, mono_smp_per_block);

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
AudioThreadHandle AudioThreadHandle::make(portaudio::System & sys) {
    portaudio::DirectionSpecificStreamParameters outParams(
                sys.defaultOutputDevice(),
                STEREO_NCHAN,
                AmplitudeFmt,
                output::OutputCallback::interleaved,
                sys.defaultOutputDevice().defaultLowOutputLatency(),
                nullptr);
    portaudio::StreamParameters params(
                portaudio::DirectionSpecificStreamParameters::null(),
                outParams,
                48000.0,
                (unsigned long) MONO_SMP_PER_BLOCK,
                paNoFlag);

    // We cannot move/memcpy due to self-reference (stream holds reference to callback).
    // C++17 guaranteed copy elision only works on prvalues, not locals.
    // So let constructor initialize fields in-place (our factory method cannot).
    return AudioThreadHandle{outParams, params};
}

AudioThreadHandle::AudioThreadHandle(
    portaudio::DirectionSpecificStreamParameters outParams,
    portaudio::StreamParameters params
) :
    callback(std::make_unique<OutputCallback>(
        outParams.numChannels(), (int)params.sampleRate()
    )),
    stream(std::make_unique<SelfTerminatingStream>(params, *callback))
{
    stream->start();
}

// end namespaces
}
}
