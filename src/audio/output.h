#pragma once

#include "audio.h"
//#include "document.h"

#include <portaudiocpp/PortAudioCpp.hxx>
#include <gsl/gsl>

#include <random>
#include <limits>
#include <iostream>

#define Q_DISABLE_COPY(Class) \
    Class(const Class &) = delete;\
    Class &operator=(const Class &) = delete;

#define Q_DISABLE_MOVE(Class) \
    Class(Class &&) = delete; \
    Class &operator=(Class &&) = delete;

#define Q_DISABLE_COPY_MOVE(Class) \
    Q_DISABLE_COPY(Class) \
    Q_DISABLE_MOVE(Class)

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

// Maybe the AudioThreadHandle should only be possible to return a unique_ptr.
// Then we hold a SelfTerminatingStream by value, not unique_ptr.
struct AudioThreadHandle {
    output::OutputCallback callback;

    // Holds reference to output, so declared afterwards (destruction is last-to-first).
    std::unique_ptr<pa::Stream> stream; //<pa::NonBlocking, pa::Output<Amplitude>>;

public:
    // throws PaException or PaCppException or whatever else
    static AudioThreadHandle make(pa::System & sys);

private:
    /*
    boost::noncopyable does not disable move.
    But we cannot move/memcpy due to self-reference (stream holds reference to callback).

    In my earlier exotracker-rs, the output callback received data from the synth via a queue,
    so had no self-reference issues.
    But to reduce latency, I synthesize audio within the output callback (like OpenMPT).
    Which causes self-reference, so this struct cannot be moved.
    */
    Q_DISABLE_COPY_MOVE(AudioThreadHandle)

    AudioThreadHandle(
            portaudio::DirectionSpecificStreamParameters outParams,
            portaudio::StreamParameters params
            );
};

}   // namespace audio
