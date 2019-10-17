#include "output.h"

namespace audio {
namespace output {

int OutputCallback::paCallbackFun(
        const void *inputBufferVoid,
        void *outputBufferVoid,
        unsigned long mono_smp_per_block,
        const PaStreamCallbackTimeInfo *timeInfo,
        PaStreamCallbackFlags statusFlags
        ) {
    // Convert output buffer from raw pointer into GSL span.
    std::ptrdiff_t stereo_smp_per_block = stereo_nchan * mono_smp_per_block;
    gsl::span output{(Amplitude *) outputBufferVoid, stereo_smp_per_block};

    // Silence output buffer.
    for (Amplitude & y : output) y = 0;

    // Add mono noise.
    for (ptrdiff_t x = 0; x + stereo_nchan <= output.size(); x += stereo_nchan) {
        auto chan_y = output.subspan(x, stereo_nchan);

        Amplitude rand_y = this->distribution(this->generator);
        for (Amplitude & y : chan_y) {
            y += rand_y / 3;
        }
    }

    return PaStreamCallbackResult::paContinue;
}

}

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

static int const stereo_nchan = 2;
static uintptr_t const mono_smp_per_block = 64;


/// Why factory method and not constructor?
/// So we can calculate values (like sampling rate) used in multiple places.
AudioThreadHandle AudioThreadHandle::make(portaudio::System & sys) {
    portaudio::DirectionSpecificStreamParameters outParams(
                sys.defaultOutputDevice(),
                stereo_nchan,
                AmplitudeFmt,
                output::OutputCallback::interleaved,
                sys.defaultOutputDevice().defaultLowOutputLatency(),
                nullptr);
    portaudio::StreamParameters params(
                portaudio::DirectionSpecificStreamParameters::null(),
                outParams,
                48000.0,
                (unsigned long) mono_smp_per_block,
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
    callback(outParams.numChannels()),
    stream(std::make_unique<SelfTerminatingStream>(params, callback))
{
    stream->start();
}

}
