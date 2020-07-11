#include "pattern.h"
#include "edit_util/kv.h"

#include <algorithm>  // std::swap
#include <memory>  // make_unique. <memory> is already included for EditBox though.

namespace edit::pattern {

using namespace doc;

/// Since BaseEditCommand has a virtual destructor,
/// subclasses cannot be aggregate-initialized (requiring constructor boilerplate).
/// So instead make BaseEditCommand subclasses hold data (Body _inner),
/// which can be aggregate-initialized.
/// This approach also allows us to define cloning once,
/// instead of repeating the boilerplate in each command class.
template<typename Body>
struct EditCommand : BaseEditCommand {
    Body _inner;

    EditCommand(Body inner)
        : BaseEditCommand{}, _inner{std::move(inner)}
    {}

    EditBox box_clone() override {
        return std::make_unique<EditCommand>(_inner);
    }

    void apply_swap(doc::Document & document) override {
        return _inner.apply_swap(document);
    }
};

template<typename Body>
static EditBox make_command(Body inner) {
    return std::make_unique<EditCommand<Body>>(std::move(inner));
}

/// Implements EditCommand. Other classes can store a vector of multiple PatternEdit.
struct PatternEdit {
    SeqEntryIndex _seq_entry_index;
    ChipIndex _chip;
    ChannelIndex _channel;

    doc::EventList _events;

    void apply_swap(doc::Document & document) {
        // TODO #ifndef NDEBUG, assert all of _events are non-empty.
        auto & doc_events =
            document.sequence[_seq_entry_index].chip_channel_events[_chip][_channel];
        doc_events.swap(_events);
    }
};

/// Erase all empty elements of an entire EventList (not a slice).
static void erase_empty(EventList & v) {
    auto ev_empty = [](TimedRowEvent & e) {
        return e.v == RowEvent{};
    };
    // remove empty events, consolidate the rest into [begin, new_end).
    auto new_end = std::remove_if(v.begin(), v.end(), ev_empty);
    // erase [new end, old end).
    v.erase(new_end, v.end());
}

EditBox delete_cell(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    SubColumn subcolumn,
    PatternAndBeat time
) {
    // Copy event list.
    doc::EventList events =
        document.sequence[time.seq_entry_index].chip_channel_events[chip][channel];

    // Erase certain event fields, based on where the cursor was positioned.
    {
        edit_util::kv::KV kv{events};

        auto ev_begin = kv.beat_begin(time.beat);
        auto ev_end = kv.beat_end(time.beat);
        auto p = &subcolumn;

        for (auto it = ev_begin; it != ev_end; it++) {
            auto & event = it->v;

            if (std::get_if<subcolumns::Note>(p)) {
                event.note = {};
                event.instr = {};
                // TODO v.volume = {};
            } else
            if (std::get_if<subcolumns::Instrument>(p)) {
                event.instr = {};
            } else
            if (std::get_if<subcolumns::Volume>(p)) {
                // TODO v.volume = {};
            } else
            if (auto eff = std::get_if<subcolumns::EffectName>(p)) {
                // TODO v.effects[eff->effect_col]
            } else
            if (auto eff = std::get_if<subcolumns::EffectValue>(p)) {
                // TODO v.effects[eff->effect_col]
            }
        }
    }

    // If we erase all fields from an event, remove the event entirely.
    erase_empty(events);

    return make_command(PatternEdit{
        time.seq_entry_index, chip, channel, std::move(events)
    });
}

EditBox insert_note(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    PatternAndBeat time,
    doc::Note note
) {
    // We don't need to check if the user is inserting "no note",
    // because it has type optional<Note> and value nullopt.

    // Copy event list.
    doc::EventList events =
        document.sequence[time.seq_entry_index].chip_channel_events[chip][channel];

    // Insert note.
    edit_util::kv::KV kv{events};
    auto & ev = kv.get_or_insert(time.beat);
    ev.v.note = note;

    return make_command(PatternEdit{
        time.seq_entry_index, chip, channel, std::move(events)
    });
}

// TODO write test to ensure subcolumn/selection deletion clears empty events.

}
