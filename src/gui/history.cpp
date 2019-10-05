#include "history.h"

namespace gui {
namespace history {

const History::BoxT History::get() const {
    return current.load();
}

void History::push(History::BoxT item) {
    // Do I add support for tree-undo?
    redo_stack.clear();

    // TODO if HistoryFrame.can_merge(current.load()), store to current and discard old value.

    // Move new state into current. Move current state into undo stack.
    undo_stack.emplace_back(current.exchange(item));
}

Success History::undo() {
    if (undo_stack.empty()) {
        return Success{false};
    }

    // Pop undo into current state. Move current state into redo.
    redo_stack.emplace_back(current.exchange(undo_stack.back()));

    // vector.pop_back() returns void because it's impossible to return the object exception-safely.
    // https://stackoverflow.com/a/12600477
    // I think "dealing with other people's exceptions" is painful.
    undo_stack.pop_back();

    return Success{true};
}

Success History::redo() {
    if (redo_stack.empty()) {
        return Success{false};
    }

    // Pop redo into current state. Move current state into undo.
    undo_stack.emplace_back(current.exchange(redo_stack.back()));
    redo_stack.pop_back();

    return Success{true};
}

// namespaces
}
}
