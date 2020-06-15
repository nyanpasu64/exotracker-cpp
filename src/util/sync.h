#pragma once

#if !defined(EXO_BIN) && !defined(UNITTEST)
#error Mutexes should only be used within exotracker-bin or exotracker-tests \
(only the GUI supports document mutation and needs synchronization).
#endif

#include <memory>
#include <mutex>
#include <optional>

// `namespace sync` conflicts with `void sync(void)` in unistd.h (POSIX).
namespace util::sync {

template<typename Ptr>
class [[nodiscard]] Guard {
    std::unique_lock<std::mutex> _guard;
    Ptr _value;

private:
    /// Don't lock std::unique_lock; the factory methods will lock it.
    Guard(std::mutex & mutex, Ptr value) :
        _guard{mutex, std::defer_lock_t{}}, _value{value}
    {}

public:
    static Guard make(std::mutex & mutex, Ptr value) {
        Guard guard{mutex, value};
        guard._guard.lock();
        return guard;
    }

    static std::optional<Guard> try_make(std::mutex & mutex, Ptr value) {
        Guard guard{mutex, value};
        if (guard._guard.try_lock()) {
            return guard;
        } else {
            return {};
        }
    }

    explicit operator bool() const noexcept {
        return _guard.operator bool();
    }

    typename std::pointer_traits<Ptr>::element_type & operator*() {
        return *_value;
    }

    Ptr operator->() {
        return _value;
    }
};

/// A fake rwlock for two threads, where only one edits the contents.
/// Implemented as a single mutex only held by one thread.
///
/// Only the GUI thread can legally obtain exclusive references (when writing),
/// by acquiring the mutex first.
/// The GUI thread can obtain shared references (when reading) without acquiring the mutex!
/// The audio thread can obtain a *shared* reference by acquiring the mutex.
///
/// Should be faster than a true rwlock,
/// but doesn't generalize to >2 threads or >1 writer thread.
template<typename T>
class FakeRwLock {
    mutable std::mutex _mutex;
    T _value;

public:
    explicit FakeRwLock(T && value) : _value(std::move(value)) {}

    using ReadPtr = T const *;
    using WritePtr = T *;

    using ReadGuard = Guard<ReadPtr>;
    using WriteGuard = Guard<WritePtr>;

    /// Only call this in the same thread which calls gui_write().
    ReadPtr gui_read() const {
        return &_value;
    }

    /// Only call this in the GUI thread.
    WriteGuard gui_write() {
        return WriteGuard::make(_mutex, &_value);
    }

    /// Can be safely called in the audio thread.
    std::optional<ReadGuard> try_read() const {
        return ReadGuard::try_make(_mutex, &_value);
    }
};

}
