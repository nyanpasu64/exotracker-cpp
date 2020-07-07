#pragma once

#include "doc.h"
#include "util/copy_move.h"

#include <memory>

namespace edit {

class BaseEditCommand;

// Non-null pointer.
using EditBox = std::unique_ptr<BaseEditCommand>;

// Nullable pointer.
using MaybeEditBox = std::unique_ptr<BaseEditCommand>;

class BaseEditCommand {
public:
    // Copy/move constructor are defaulted to allow subclasses to copy/move themselves.
    // Object slicing is prevented by pure-virtual methods.

    // Mark destructor as virtual.
    virtual ~BaseEditCommand() {};

    /// Not bounded-time.
    /// Called on the GUI thread. Return value is sent to the audio thread.
    virtual EditBox box_clone() = 0;

    /// Bounded-time. Safe to call on both GUI and audio thread.
    ///
    /// Simple but unintuitive API. Atomic CAS is also simple but unintuitive.
    ///
    /// For mutations, apply_swap() swaps the command state and document state.
    ///
    /// Additions/subtractions are the same subclass holding an option.
    /// apply_swap() either fills option from document, or moves option to document.
    ///
    /// You can call apply_swap() repeatedly on the same document
    /// to repeatedly undo/redo the same action.
    /// After performing an EditCommand, store it as an undoer.
    /// After undoing, store it as a redoer.
    virtual void apply_swap(doc::Document & document) = 0;
};

}
