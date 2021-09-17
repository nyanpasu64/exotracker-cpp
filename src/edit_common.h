#pragma once

#include "edit/modified_common.h"
#include "doc.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace edit {

class BaseEditCommand;

/// Non-null pointer.
///
/// All edit commands return an EditBox with no indication of cursor movement.
/// PatternEditor is responsible for moving MainWindow's cursor,
/// and MainWindow is responsible for saving old/new cursor positions in a CursorEdit.
///
/// Is this a good design? I don't know.
using EditBox = std::unique_ptr<BaseEditCommand>;

// Nullable pointer.
using MaybeEditBox = std::unique_ptr<BaseEditCommand>;

using modified::ModifiedInt;
using modified::ModifiedFlags;

class [[nodiscard]] BaseEditCommand {
public:
    // Copy/move constructor are defaulted to allow subclasses to copy/move themselves.
    // Object slicing is prevented by pure-virtual methods.

    // Mark destructor as virtual.
    virtual ~BaseEditCommand() = default;

    /// Not bounded-time.
    /// Called on the GUI thread when an edit needs to be sent to the audio thread.
    ///
    /// By default, this simply clones the object behind the pointer to a new EditBox.
    /// Certain subclasses override this method to return a different type, which
    /// precomputes data to make `apply_swap()` faster, at the cost of using more RAM.
    ///
    /// See DESIGN.md#clone_for_audio for justification.
    [[nodiscard]] virtual EditBox clone_for_audio(doc::Document const& doc) const = 0;

    /// Bounded-time if EditBox was created by `clone_for_audio()`.
    /// Called on both GUI and audio threads.
    ///
    /// Simpler to implement than conventional undo systems with separate undo/redo
    /// methods.
    ///
    /// For mutations, apply_swap() swaps the command state and document state.
    ///
    /// Additions/subtractions are the same subclass holding an option.
    /// apply_swap() either fills option from document, or moves option to document.
    ///
    /// You can call apply_swap() repeatedly on the same document
    /// to repeatedly undo/redo the same action.
    /// After applying a BaseEditCommand, store it as an undoer.
    /// After undoing, store it as a redoer.
    virtual void apply_swap(doc::Document & document) = 0;

    /// Upon initially pushing an operation `curr` into undo history,
    /// History calls curr.can_merge(prev) *after* calling curr.apply_swap().
    ///
    /// It's only safe to merge multiple edits
    /// if the first edit edits the same location as or dominates the second,
    /// meaning that undoing the first edit produces the same document
    /// whether the second edit was undone or not.
    ///
    /// If you want two edit operations to merge,
    /// both must entirely replace the same section of the document.
    virtual bool can_merge(BaseEditCommand & prev) const = 0;

    /// Returns a bitflag specifying which parts of the document are modified.
    /// Called by the audio thread to invalidate/recompute sequencer state.
    ///
    /// (This could be a base-class field instead, I guess.)
    [[nodiscard]] virtual ModifiedFlags modified() const = 0;
};

}
