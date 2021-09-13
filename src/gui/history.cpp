#include "history.h"

namespace gui::history {

History::History(doc::Document initial_state)
    : _document(std::move(initial_state))
{}

void History::push(CursorEdit command) {
    // Do I add support for tree-undo?
    redo_stack.clear();

    // TODO if HistoryFrame.can_merge(current.load()), store to current and discard old value.

    // Move new state into current.
    command.apply_swap(_document);

    if (undo_stack.size()) {
        auto & prev = undo_stack.back();

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
    undo_stack.push_back(std::move(command));
}

MaybeCursorEdit History::get_undo() const {
    if (undo_stack.empty()) {
        return {};
    }
    return undo_stack.back().clone(_document);
}

void History::undo() {
    // vector.pop_back() returns void because it's impossible to return the object exception-safely.
    // https://stackoverflow.com/a/12600477
    // I think "dealing with other people's exceptions" is painful.

    if (undo_stack.empty()) {
        return;
    }

    // Pop undo.
    CursorEdit command = std::move(undo_stack.back());
    undo_stack.pop_back();

    // Apply to document.
    command.apply_swap(_document);

    // Push to redo.
    redo_stack.push_back(std::move(command));
}

edit::MaybeCursorEdit History::get_redo() const {
    if (redo_stack.empty()) {
        return {};
    }
    return redo_stack.back().clone(_document);
}

void History::redo() {
    if (redo_stack.empty()) {
        return;
    }

    // Pop redo.
    CursorEdit command = std::move(redo_stack.back());
    redo_stack.pop_back();

    // Apply to document.
    command.apply_swap(_document);

    // Push to undo.
    undo_stack.push_back(std::move(command));
}

History const EMPTY_HISTORY = History(doc::DocumentCopy{});

}
