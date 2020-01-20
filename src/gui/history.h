#pragma once

#include "doc.h"

#include <boost/core/noncopyable.hpp>
#include <vector>
#include <memory>

namespace gui {
namespace history {

struct Success {
    bool succeeded;
};


/// Class is intended to be atomic.
/// push() and undo() and redo() can be called from one thread (GUI thread),
/// while get() can be called from any thread desired (audio thread).
///
/// This class is approximately correct at best ;)
class History : boost::noncopyable, public doc::GetDocument {
public:
//    using UnsyncT = doc::HistoryFrame;
    using UnsyncT = doc::Document;

public:
    History(doc::Document initial_state);
    ~History() override = default;

    /// Called from UI or audio thread. Non-blocking and thread-safe.
    ///
    /// If the audio thread calls get(),
    /// the main thread deletes its copy of `current` leaving the audio thread the last owner,
    /// and the audio thread deletes the resulting Immer object,
    /// it may have unpredictable delays due to some nested elements being on the C++ heap.
    ///
    /// But this is unlikely, since the main thread does not delete `current`
    /// but instead moves it to undo/redo (and redo is only cleared upon new actions).
    UnsyncT const & get() const;

    /// Called from UI thread. Switch `get()` to previous state.
    Success undo();

    /// Called from UI thread. Switch `get()` to next state.
    Success redo();

    // impl doc::GetDocument
    doc::Document const & get_document() const override {
        return get();
    }
};

// namespaces
}
}
