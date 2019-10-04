#pragma once

#include "audio.h"
//#include "document.h"

#include <portaudiocpp/PortAudioCpp.hxx>
#include <gsl/gsl>

#include <random>
#include <limits>
#include <iostream>

namespace audio {
namespace output {

namespace pa = portaudio;

class OutputCallback : public pa::CallbackInterface {

    int stereo_nchan;

    std::default_random_engine generator;
    std::uniform_int_distribution<Amplitude> distribution{
        -std::numeric_limits<Amplitude>::max(),
        std::numeric_limits<Amplitude>::max()
    };

public:
    // interleaved=true => outputBufferVoid: [smp#, * nchan + chan#] Amplitude
    // interleaved=false => outputBufferVoid: [chan#][smp#]Amplitude
    // interleaved=false was added to support ASIO's native representation.
    static const bool interleaved = true;

    explicit OutputCallback(int stereo_nchan) : stereo_nchan(stereo_nchan) {}

    // impl pa::CallbackInterface

    // returns PaStreamCallbackResult.
    int paCallbackFun(const void *inputBufferVoid, void *outputBufferVoid, unsigned long mono_smp_per_block,
                      const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags) override;

};
}   // namespace output

struct AudioThreadHandle {
    // throws PaException or PaCppException or whatever else
    AudioThreadHandle(pa::System & sys);

    output::OutputCallback callback;

    // Holds reference to output, so declared afterwards (destruction is last-to-first).
    std::unique_ptr<pa::Stream> stream; //<pa::NonBlocking, pa::Output<Amplitude>>;
};

}   // namespace audio
