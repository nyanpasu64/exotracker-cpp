#include "history.h"

namespace gui::history {

History::History(doc::Document initial_state)
    : _document(std::move(initial_state))
{}

void History::push(UndoFrame command) {
    // Do I add support for tree-undo?
    _redo_stack.clear();

    // TODO if HistoryFrame.can_merge(current.load()), store to current and discard old value.

    // Move new state into current.
    command.edit->apply_swap(_document);
    _dirty = true;

    if (_undo_stack.size()) {
        auto & prev = _undo_stack.back();

        // Only true if:
        // - prev and command should be combined in undo history.
        // - prev and command mutate the same state,
        //   so History can discard command entirely after calling apply_swap().
        if (command.edit->can_merge(*prev.edit)) {
            prev.after_cursor = command.after_cursor;

            // Discard current state. We only keep new state and previous state.
            return;
        }
    }
    // Move current state into undo stack.
    _undo_stack.push_back(std::move(command));
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
