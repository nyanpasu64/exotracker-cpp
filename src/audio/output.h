#pragma once

/// Sends audio to computer speakers.
///
/// Synth code generates audio whenever the output callback runs.
/// It does not operate independently.

#include "util/macros.h"

#include <portaudiocpp/PortAudioCpp.hxx>

#include <memory>

namespace audio {
namespace output {

namespace pa = portaudio;

struct AudioThreadHandle {
    // output.h does not contain #include "synth.h",
    // and only exposes output::OutputCallback via unique_ptr<pa::CallbackInterface>.
    //
    // Advantage: Changing the layout of synth::OverallSynth
    //  will not recompile OutputCallback and everything that #includes output.h.
    //
    // No disadvantage: No speed loss from indirection or reduced locality,
    //  since pa::Stream accesses via pointer anyway.
    std::unique_ptr<portaudio::CallbackInterface> callback;

    // Holds reference to `callback`, so declared afterwards
    // (destruction is last-to-first).
    // Barely accessed (->start()), so indirection is not a speed concern.
    std::unique_ptr<pa::Stream> stream;

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
