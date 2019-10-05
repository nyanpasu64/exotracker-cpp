#pragma once

#include "document.h"

#include <immer/atom.hpp>
#include <immer/box.hpp>
#include <boost/core/noncopyable.hpp>
#include <utility>
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
class History : boost::noncopyable {
private:
    using UnsyncT = doc::HistoryFrame;
    using BoxT = immer::box<UnsyncT>;

    /// immer::atom<T> stores immer::box<T>, not T. This is why we parameterize with unboxed T.
    immer::atom<UnsyncT> current;
    std::vector<BoxT> undo_stack;
    std::vector<BoxT> redo_stack;

public:
    virtual ~History() = default;

    /// Called from UI or audio thread. Non-blocking and thread-safe.
    ///
    /// If the audio thread calls get(),
    /// the main thread deletes its copy of `current` leaving the audio thread the last owner,
    /// and the audio thread deletes the resulting Immer object,
    /// it may have unpredictable delays due to some nested elements being on the C++ heap.
    ///
    /// But this is unlikely, since the main thread does not delete `current`
    /// but instead moves it to undo/redo (and redo is only cleared upon new actions).
    BoxT const get() const;

    /// Called from UI thread. Clear redo stack and push a new element into the history.
    void push(BoxT item);

    /// Called from UI thread. Switch `get()` to previous state.
    Success undo();

    /// Called from UI thread. Switch `get()` to next state.
    Success redo();
};

// namespace history
}

//class DocumentHistory : public history::History
////        , doc::GetDocument
//{
////    doc::TrackPattern const & get_document() const override {
////        return get();
////    }
//};

}
