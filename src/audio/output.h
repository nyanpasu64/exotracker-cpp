#pragma once

/// Sends audio to computer speakers.
/// Intended for GUI mode with concurrent editing of the document during playback.
/// AudioThreadHandle uses locked_doc::GetDocument to handles audio/GUI locking
/// and sends a raw `Document const &` to OverallSynth.
///
/// In the absence of concurrent editing, you can use OverallSynth directly
/// and avoid locking and unlocking std::mutex.
///
/// This has precedent: libopenmpt does not talk directly to an output device,
/// but merely exposes a callback api with no knowledge of locks or PortAudio/RtAudio.
/// libopenmpt can be called via ffmpeg or foobar2000,
/// which have their own non-speaker output mechanisms.
///
/// Synth code operates on a pull model;
/// the synth callback generates audio whenever RtAudio calls the output callback.
/// By contrast, FamiTracker's synth thread pushes to a queue with backpressure.

#include "audio_common.h"
#include "callback.h"
#include "doc.h"
#include "cmd_queue.h"
#include "timing_common.h"
#include "util/copy_move.h"

#include <rtaudio/RtAudio.h>

#include <functional>  // reference_wrapper
#include <memory>

namespace audio {
namespace output {

using cmd_queue::AudioCommand;
using timing::MaybeSequencerTime;
using callback::CallbackInterface;

class AudioThreadHandle {
    // Used to shut down the stream when AudioThreadHandle is destroyed.
    std::reference_wrapper<RtAudio> _rt;

    // output.h does not contain #include "synth.h",
    // and only exposes output::OutputCallback via unique_ptr<CallbackInterface>.
    //
    // Advantage: Changing the layout of synth::OverallSynth
    //  will not recompile OutputCallback and everything that #includes output.h.
    //
    // No disadvantage: No speed loss from indirection (unsure about reduced locality),
    //  since RtAudio accesses via pointer anyway.
    std::unique_ptr<CallbackInterface> _callback;

    AudioThreadHandle(RtAudio & rt, std::unique_ptr<CallbackInterface> && callback)
        : _rt{rt}
        , _callback{std::move(callback)}
    {}

public:
    // Moving `this` is okay because `callback` is stored behind a unique_ptr.
    // But for some reason, move assignment defaults to deleted copy assignment,
    // not move assignment.
    // So explicitly opt into moves.
    DISABLE_COPY(AudioThreadHandle)
    DEFAULT_MOVE(AudioThreadHandle)

    /// Preconditions:
    /// - get_document argument must outlive returned OverallSynth.
    /// - get_document's list of chips must not change between calls.
    ///   If it changes, destroy returned OverallSynth and create a new one.
    /// - In get_document's list of chips, any APU2 must be preceded directly with APU1.
    ///
    /// throws PaException or PaCppException or whatever else
    static std::optional<AudioThreadHandle> make(
        RtAudio & rt,
        unsigned int device,
        doc::Document document,
        AudioCommand * stub_command
    );

    /// Called by GUI thread.
    inline AudioCommand * seen_command() const {
        return _callback->seen_command();
    }

    /// Called by GUI thread.
    inline MaybeSequencerTime play_time() const {
        return _callback->play_time();
    }

    ~AudioThreadHandle();
};

}   // namespace output
}   // namespace audio
