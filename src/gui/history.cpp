#include "history.h"

namespace gui::history {

History::History(doc::Document initial_state)
    : _document(std::move(initial_state))
{}

EditBox History::push(EditBox command) {
    auto clone = command->box_clone();

    // Do I add support for tree-undo?
    redo_stack.clear();

    // TODO if HistoryFrame.can_merge(current.load()), store to current and discard old value.

    // Move new state into current. Move current state into undo stack.
    command->apply_swap(_document);
    undo_stack.push_back(std::move(command));

    return clone;
}

// vector.pop_back() returns void because it's impossible to return the object exception-safely.
// https://stackoverflow.com/a/12600477
// I think "dealing with other people's exceptions" is painful.

MaybeEditBox History::undo() {
    if (undo_stack.empty()) {
        return nullptr;
    }

    // Pop undo.
    EditBox command = std::move(undo_stack.back());
    auto clone = command->box_clone();
    undo_stack.pop_back();

    // Apply to document.
    command->apply_swap(_document);

    // Push to redo.
    redo_stack.push_back(std::move(command));

    return clone;
}

MaybeEditBox History::redo() {
    if (redo_stack.empty()) {
        return nullptr;
    }

    // Pop redo.
    EditBox command = std::move(redo_stack.back());
    auto clone = command->box_clone();
    redo_stack.pop_back();

    // Apply to document.
    command->apply_swap(_document);

    // Push to undo.
    undo_stack.push_back(std::move(command));

    return clone;
}

}
