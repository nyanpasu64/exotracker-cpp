#include "history.h"

namespace gui::history {

History::History(doc::Document initial_state)
    : _document(std::move(initial_state))
{}

void History::push(CursorEdit command) {
    // Do I add support for tree-undo?
    redo_stack.clear();

    // TODO if HistoryFrame.can_merge(current.load()), store to current and discard old value.

    // Move new state into current. Move current state into undo stack.
    command.apply_swap(_document);
    undo_stack.push_back(std::move(command));
}

// vector.pop_back() returns void because it's impossible to return the object exception-safely.
// https://stackoverflow.com/a/12600477
// I think "dealing with other people's exceptions" is painful.

MaybeCursorEdit History::undo() {
    if (undo_stack.empty()) {
        return {};
    }

    // Pop undo.
    CursorEdit command = std::move(undo_stack.back());
    auto clone = command.clone();
    undo_stack.pop_back();

    // Apply to document.
    command.apply_swap(_document);

    // Push to redo.
    redo_stack.push_back(std::move(command));

    return clone;
}

MaybeCursorEdit History::redo() {
    if (redo_stack.empty()) {
        return {};
    }

    // Pop redo.
    CursorEdit command = std::move(redo_stack.back());
    auto clone = command.clone();
    redo_stack.pop_back();

    // Apply to document.
    command.apply_swap(_document);

    // Push to undo.
    undo_stack.push_back(std::move(command));

    return clone;
}

}
