#pragma once

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
template<
        typename UnsyncT,
        typename BoxT = immer::box<UnsyncT>,
        typename AtomInner = UnsyncT    // should it be BoxT?
        >
class History : boost::noncopyable {
private:
    immer::atom<AtomInner> current;
    std::vector<BoxT> undo_stack;
    std::vector<BoxT> redo_stack;

public:
    virtual ~History() = default;

    /// When the audio thread calls get() and deletes the resulting Immer object,
    /// it may have unpredictable delays due to some nested elements being on the C++ heap.
    /// But the audio callback will probably not hold the last reference to an Immer object.
    /// Instead, the undo/redo history will probably outlive an audio callback.
    BoxT const & get() const {
        return current;
    }

    /// Called from UI thread. Clear redo stack and push a new element into the history.
    void push(BoxT item) {
        // Do I add support for tree-undo?
        redo_stack.clear();

        undo_stack.emplace_back(std::move(current));
        current = item;

        return current;
    }

    /// Called from UI thread. Switch `get()` to previous state.
    Success undo() {
        if (undo_stack.empty()) {
            return Success{false};
        }

        redo_stack.emplace_back(std::move(current));
        current = undo_stack.pop_back();

        return Success{true};
    }

    /// Called from UI thread. Switch `get()` to next state.
    Success redo() {
        clear_hanging_ref();

        if (redo_stack.empty()) {
            return Success{false};
        }

        undo_stack.emplace_back(std::move(current));
        current = redo_stack.pop_back();

        return Success{true};
    }
};

// namespaces
}
}
