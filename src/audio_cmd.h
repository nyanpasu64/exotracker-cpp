#pragma once

#include "timing_common.h"
#include "util/copy_move.h"

#include <atomic>

namespace audio_cmd {
#ifndef audio_cmd_INTERNAL
#define audio_cmd_INTERNAL private
#endif

struct SeekTo {
    timing::PatternAndBeat time;

    SeekTo(timing::PatternAndBeat time)
        : time{time}
    {}

    // C++ constructor generation rules?
    // https://www.enyo.de/fw/notes/cpp-auto-members.html
    // https://en.cppreference.com/w/cpp/language/rule_of_three

    // this macro invocation is *so* hacky
    explicit DEFAULT_COPY(SeekTo)
    DEFAULT_MOVE(SeekTo)
};

/// Exposed to audio thread.
struct AudioCommand {
    std::optional<SeekTo> seek_or_stop;
    std::atomic<AudioCommand *> next;  // noncopyable, immovable

    explicit AudioCommand(std::optional<SeekTo> seek_or_stop)
        : seek_or_stop{seek_or_stop}
    {
        next.store(nullptr, std::memory_order_relaxed);
    }
};

/// All methods are not thread-safe.
/// This class should only be held/called by the GUI thread.
class [[nodiscard]] CommandQueue {
audio_cmd_INTERNAL:
    AudioCommand * _begin;  // non-null
    AudioCommand * _end;  // non-null, may be equal

public:
    CommandQueue();
    ~CommandQueue();
    void clear();

    DISABLE_COPY(CommandQueue)
    CommandQueue(CommandQueue && other) noexcept;
    CommandQueue & operator=(CommandQueue && other) noexcept;

    /// The return value is atomically stored into the audio synth,
    /// and read by the audio thread.
    AudioCommand * begin() const {
        return _begin;
    }

    AudioCommand * end() const {
        return _end;
    }

    template <typename ...Args>
    void push(Args && ...args) {
        push_ptr(new AudioCommand(std::forward<Args>(args)...));
    }

private:
    void push_ptr(AudioCommand * elem);

public:
    // no return value because exception safety or something?
    // what is exception safety help
    void pop();
};

}
