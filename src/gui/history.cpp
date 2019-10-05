#include "history.h"

namespace gui {
namespace history {

History::History(doc::TrackPattern initial_state) : current{initial_state} {}

History::BoxT const History::get() const {
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

doc::TrackPattern dummy_pattern() {
    doc::TrackPattern pattern;

    pattern.nbeats = 4;

    using Frac = doc::BeatFraction;

    // TimeInPattern, RowEvent
    {
        auto & channel_ref = pattern.channels[doc::ChannelId::Test1];
        channel_ref = doc::KV{channel_ref}
                .set_time({0, 0}, {0})
                .set_time({{1, 3}, 0}, {1})
                .set_time({{2, 3}, 0}, {2})
                .set_time({1, 0}, {3})
                .set_time({1 + Frac{1, 4}, 0}, {4})
                .channel_events;
    }
    {
        auto & channel_ref = pattern.channels[doc::ChannelId::Test2];
        channel_ref = doc::KV{channel_ref}
                .set_time({2, 0}, {102})
                .set_time({3, 0}, {103})
                .channel_events;
    }

    return pattern;
}

// namespaces
}
}
