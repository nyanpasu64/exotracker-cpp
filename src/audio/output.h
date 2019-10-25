#pragma once

/// Sends audio to computer speakers.
///
/// Synth code generates audio whenever the output callback runs.
/// It does not operate independently.

#include "synth.h"
#include "util/macros.h"

#include <Blip_Buffer/Blip_Buffer.h>
#include <portaudiocpp/PortAudioCpp.hxx>

#include <random>
#include <limits>
#include <iostream>

namespace audio {
namespace output {

namespace pa = portaudio;

// Const variables have internal linkage, unless explicitly declared extern.
// So this won't be defined multiple times, even if I omit static.
const auto AmplitudeFmt = portaudio::SampleDataFormat::INT16;

/// I may extract a class Synth,
/// and change OutputCallback to PortAudioCallback (a thin wrapper around Synth).
/// I'll only do that once I have multiple API consumers
/// (PortAudio, RtAudio, or WAV export) and can design a good API for all of them.
class OutputCallback : public pa::CallbackInterface {
    synth::OverallSynth synth;

public:
    // interleaved=true => outputBufferVoid: [smp#, * nchan + chan#] Amplitude
    // interleaved=false => outputBufferVoid: [chan#][smp#]Amplitude
    // interleaved=false was added to support ASIO's native representation.
    static const bool interleaved = true;

    OutputCallback(int stereo_nchan, int smp_per_s) :
        synth{stereo_nchan, smp_per_s}
    {}

    // impl pa::CallbackInterface

    // returns PaStreamCallbackResult.
    int paCallbackFun(const void *inputBufferVoid, void *outputBufferVoid, unsigned long mono_smp_per_block,
                      const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags) override;

};

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
    DISABLE_COPY_MOVE(AudioThreadHandle)

    AudioThreadHandle(
            portaudio::DirectionSpecificStreamParameters outParams,
            portaudio::StreamParameters params
            );
};

}   // namespace output
}   // namespace audio
