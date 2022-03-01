#include "history.h"

namespace gui::history {

History::History(doc::Document initial_state)
    : _document(std::move(initial_state))
{}

void History::push(UndoFrame command) {
    // Preconditions: `_document` holds the initial state, and `command` holds the new
    // state.

    // Apply the `command` edit. This swaps `_document` and `command`'s states.
    command.edit->apply_swap(_document);
    // Now `command` holds the initial state, and `_document` holds the new state.

    // Mark document as edited. (Currently undoing changes doesn't mark document as
    // clean.)
    _dirty = true;

    if (command.edit->save_in_history()) {
        // If `command` is undoable, clear `_redo_stack` before pushing to
        // `_undo_stack` or altering `prev`. We *could* skip clearing the redo stack if
        // `save_command` is false, but that would introduce an inconsistency (and I
        // may eventually change the code to never merge edits if the redo stack is
        // non-empty).
        _redo_stack.clear();

        bool save_command = true;
        if (_undo_stack.size()) {
            // `prev` holds previous state (before initial).
            auto & prev = _undo_stack.back();

            // In some cases (like repeatedly adjusting tempo), we want to merge
            // `command` and `prev` into a single undo step, keeping the previous and
            // new states but discarding the initial state.
            //
            // If `prev` and `command` mutate the same field, History can discard the
            // initial state (stored in the `command` we applied), and only keep the
            // previous and new states (stored in `prev` and `_document`).
            if (command.edit->can_merge(*prev.edit)) {
                save_command = false;

                // `prev.after_cursor` currently holds part of the initial state.
                // Replace it with the new state (`command.after_cursor`).
                prev.after_cursor = command.after_cursor;
            }
        }

        // If we want to preserve the initial state, push the `command` we applied onto
        // the undo stack.
        if (save_command) {
            _undo_stack.push_back(std::move(command));
        }
    }
}

bool History::can_undo() const {
    return !_undo_stack.empty();
}

bool History::can_redo() const {
    return !_redo_stack.empty();
}

MaybeCursorEdit History::try_undo() {
    // vector.pop_back() returns void because it's impossible to return the object exception-safely.
    // https://stackoverflow.com/a/12600477
    // I think "dealing with other people's exceptions" is painful.

    if (_undo_stack.empty()) {
        return {};
    }

    // Pop undo command.
    UndoFrame command = std::move(_undo_stack.back());
    _undo_stack.pop_back();

    // Clone undo command for audio thread.
    auto cursor_edit = CursorEdit {
        .edit = command.edit->clone_for_audio(_document),
        .cursor = command.before_cursor,
    };

    // Apply to document.
    command.edit->apply_swap(_document);
    _dirty = true;

    // Push to redo.
    _redo_stack.push_back(std::move(command));

    return cursor_edit;
}

MaybeCursorEdit History::try_redo() {
    if (_redo_stack.empty()) {
        return {};
    }

    // Pop redo command.
    UndoFrame command = std::move(_redo_stack.back());
    _redo_stack.pop_back();

    // Clone redo command for audio thread.
    auto cursor_edit = CursorEdit {
        .edit = command.edit->clone_for_audio(_document),
        .cursor = command.after_cursor,
    };

    // Apply to document.
    command.edit->apply_swap(_document);
    _dirty = true;

    // Push to undo.
    _undo_stack.push_back(std::move(command));

    return cursor_edit;
}

History const EMPTY_HISTORY = History(doc::DocumentCopy{});

}
