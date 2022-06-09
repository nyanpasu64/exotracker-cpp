#pragma once

#include "edit_common.h"
#include "timing_common.h"
#include "util/copy_move.h"

#include <atomic>
#include <memory>
#include <variant>

namespace cmd_queue {
#ifndef audio_cmd_INTERNAL
#define audio_cmd_INTERNAL private
#endif

struct PlayFrom {
    doc::TickT time;

    PlayFrom(doc::TickT time)
        : time{time}
    {}

    // C++ constructor generation rules?
    // https://www.enyo.de/fw/notes/cpp-auto-members.html
    // https://en.cppreference.com/w/cpp/language/rule_of_three

    // this macro invocation is *so* hacky
    explicit DEFAULT_COPY(PlayFrom)
    DEFAULT_MOVE(PlayFrom)
};

struct StopPlayback {};

using edit::EditBox;

using MessageBody = std::variant<PlayFrom, StopPlayback, EditBox>;

/// Exposed to audio thread.
struct AudioCommand {
    MessageBody msg;
    std::atomic<AudioCommand *> next;  // noncopyable, immovable

    explicit AudioCommand(MessageBody && body)
        : msg{std::move(body)}
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
    AudioCommand * begin() const noexcept {
        return _begin;
    }

    AudioCommand * end() const noexcept {
        return _end;
    }

    bool is_empty() const noexcept {
        return _begin == _end;
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
