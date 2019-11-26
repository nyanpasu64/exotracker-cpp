#include "history.h"

#include "audio/synth/chip_kinds_common.h"
#include "util/macros.h"

#include <cassert>

namespace gui {
namespace history {

namespace chip_kinds = audio::synth::chip_kinds;

History::History(doc::Document initial_state) :
    current{doc::HistoryFrame{initial_state}}
{}

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

doc::Document dummy_document() {
    using Frac = doc::BeatFraction;

    doc::Document::ChipList::transient_type chips;
    doc::SequenceEntry::ChipChannelEvents::transient_type chip_channel_events;

    // chip 0
    {
        auto const chip_kind = chip_kinds::ChipKind::Apu1;
        using ChannelID = chip_kinds::Apu1ChannelID;

        chips.push_back(chip_kind);
        chip_channel_events.push_back([]() {
            doc::SequenceEntry::ChannelToEvents::transient_type channel_events;

            channel_events.push_back([]() {
                // TimeInPattern, RowEvent
                doc::EventList events = doc::KV{{}}
                    .set_time({0, 0}, {0})
                    .set_time({{1, 3}, 0}, {1})
                    .set_time({{2, 3}, 0}, {2})
                    .set_time({1, 0}, {3})
                    .set_time({1 + Frac{2, 3}, 0}, {4})
                    .event_list;
                return events;
            }());

            channel_events.push_back([]() {
                doc::EventList events = doc::KV{{}}
                    .set_time({2, 0}, {102})
                    .set_time({3, 0}, {103})
                    .event_list;
                return events;
            }());

            release_assert(channel_events.size() == (int)ChannelID::COUNT);
            return channel_events.persistent();
        }());
    }

    return doc::Document {
        .chips = chips.persistent(),
        .pattern = doc::SequenceEntry {
            .nbeats = 4,
            .chip_channel_events = chip_channel_events.persistent(),
        },
        .sequencer_options = doc::SequencerOptions{
            .ticks_per_beat = 24,
        }
    };
}

// namespaces
}
}
