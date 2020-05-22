#pragma once

#include "doc.h"
#include "locked_doc.h"
#include "command_common.h"
#include "util/sync.h"

#include <boost/core/noncopyable.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace gui {
namespace history {

struct Success {
    bool succeeded;
};

/// A class storing two Document instances as a double buffer.
/// This allows the audio thread to fetch the current Document wait-free,
/// and GUI thread to edit document (may block),
/// but not at the same time.
class DocumentStore {
public:
    using LockedDoc = locked_doc::LockedDoc;
    using ReadPtr = locked_doc::ReadPtr;
    using ReadGuard = locked_doc::ReadGuard;

private:
    // Only ever 0 or 1.
    std::atomic_uint8_t _front_index = 0;
    // Indexed by _front_index or 1 - _front_index.
    std::array<LockedDoc, 2> _documents;

public:
    // document? rvalue reference? i have no fucking clue

    // Copy? Move?
    // https://www.codesynthesis.com/~boris/blog/2012/06/19/efficient-argument-passing-cxx11-part1/
    // List initialization proceeds in left-to-right order.
    // https://en.cppreference.com/w/cpp/language/list_initialization#Notes
    explicit DocumentStore(doc::Document document);

    // Public API:

    /// Only call this in the GUI thread.
    /// No locks are acquired.
    ReadPtr gui_get_document() const;

    /// Can be safely called in the audio thread.
    /// Locks are acquired, but this is hopefully wait-free (or close to it).
    ReadGuard get_document() const;

    /// Called by History when the user edits the document.
    /// Should not be called elsewhere.
    ///
    /// Only called from the GUI thread.
    void history_apply_change(command::Command command);
};

/// Class is intended to be atomic.
/// push() and undo() and redo() can be called from one thread (GUI thread),
/// while get() can be called from any thread desired (audio thread).
///
/// This class is approximately correct at best ;)
class History : boost::noncopyable, public locked_doc::GetDocument {
private:
    DocumentStore _store;

public:
    History(doc::Document initial_state) : _store(std::move(initial_state)) {}
    ~History() override = default;

    // impl locked_doc::GetDocument
    /// Called from UI or audio thread. Non-blocking and thread-safe.
    DocumentStore::ReadGuard get_document() const override {
        return _store.get_document();
    }

    DocumentStore::ReadPtr gui_get_document() {
        return _store.gui_get_document();
    }

    /// Called from UI thread. Switch `get()` to previous state.
    Success undo();

    /// Called from UI thread. Switch `get()` to next state.
    Success redo();
};

// namespaces
}
}
