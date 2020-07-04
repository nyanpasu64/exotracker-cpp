#define audio_cmd_INTERNAL public
#include "audio_cmd.h"

#include "util/release_assert.h"

namespace audio_cmd {

// impl
void init(CommandQueue & self) {
    self._begin = self._end = new AudioCommand{StopPlayback{}};
}

/// Only run this when there are no live readers left.
void destroy_all(CommandQueue & self) {
    if (self._begin == nullptr) {
        return;
    }

    while (auto next = self._begin->next.load(std::memory_order_relaxed)) {
        auto destroy = self._begin;
        self._begin = next;
        delete destroy;
    }

    release_assert(self._begin == self._end);
    delete self._begin;
    self._begin = self._end = nullptr;
}

CommandQueue::CommandQueue() {
    init(*this);
}

CommandQueue::~CommandQueue() {
    destroy_all(*this);
}

void CommandQueue::clear() {
    destroy_all(*this);
    init(*this);
}

CommandQueue::CommandQueue(CommandQueue && other) noexcept
    : _begin{other._begin}, _end{other._end}
{
    other._begin = other._end = nullptr;
}

CommandQueue & CommandQueue::operator=(CommandQueue && other) noexcept {
    // If we're not trying to move the object into itself...
    if (this != &other) {
        destroy_all(*this);
        _begin = other._begin;
        _end = other._end;
        other._begin = other._end = nullptr;
    }
    return *this;
}

void CommandQueue::push_ptr(AudioCommand * elem) {
    // Paired with synthesize_overall() load(acquire).
    _end->next.store(elem, std::memory_order_release);
    _end = elem;
}

void CommandQueue::pop() {
    release_assert(_begin != _end);

    auto next = _begin->next.load(std::memory_order_relaxed);
    release_assert(next);

    auto destroy = _begin;
    _begin = next;
    delete destroy;
}

}
