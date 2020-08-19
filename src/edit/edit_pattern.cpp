#include "edit_pattern.h"
#include "edit_impl.h"
#include "edit/modified.h"
#include "doc_util/event_search.h"
#include "util/typeid_cast.h"

#include <algorithm>  // std::swap
#include <cassert>
#include <memory>  // make_unique. <memory> is already included for EditBox though.
#include <stdexcept>

namespace edit::edit_pattern {

using namespace doc;
using edit_impl::make_command;

struct MultiDigitEdit {
    MultiDigitField field;
    int digit_index;
};

/// assert() only takes effect on debug builds.
/// On release builds, skip coalescing instead.
#define assert_or_false(EXPR) \
    assert((EXPR)); \
    if (!(EXPR)) return false

/// Implements EditCommand. Other classes can store a vector of multiple PatternEdit.
struct PatternEdit {
    SeqEntryIndex _seq_entry_index;
    ChipIndex _chip;
    ChannelIndex _channel;

    doc::EventList _events;

    std::optional<MultiDigitEdit> _multi_digit{};
    ModifiedFlags _modified = ModifiedFlags::Patterns;

    void apply_swap(doc::Document & document) {
        auto & doc_events =
            document.sequence[_seq_entry_index].chip_channel_events[_chip][_channel];
        for (auto & ev : doc_events) {
            assert(ev.v != doc::RowEvent{});
            (void) ev;
        }
        doc_events.swap(_events);
    }

    bool can_coalesce(BaseEditCommand & prev) const {
        /*
        Invariant: the GUI pushes "second digit" edit commands
        only after matching "first digit" commands.

        What stops you from inserting a "second digit" in the wrong channel/field/time?
        All cursor movement (CursorAndSelection::set()) resets digit to 0.

        What stops you from inserting a "second digit" after a non-first-digit?
        All edits (MainWindow::push_edit()) reset digit to 0,
        except for entering the first digit.

        Right now, all undo/redo operations set the cursor position
        (which resets digit to 0).
        If non-cursor-moving undo/redo operations are added,
        and if they don't explicitly reset the cursor digit to 0,
        the resulting effects will be difficult to understand
        and may violate the invariant.
        */

        if (_multi_digit && _multi_digit->digit_index == 1) {
            using ImplPatternEdit = edit_impl::ImplEditCommand<PatternEdit>;

            // Coalesce first/second edits of the same two-digit field.
            auto prev_pattern_edit_maybe = typeid_cast<ImplPatternEdit *>(&prev);
            assert_or_false(prev_pattern_edit_maybe);
            PatternEdit & prev = *prev_pattern_edit_maybe;

            assert_or_false(prev._multi_digit);
            assert_or_false(prev._multi_digit->field == _multi_digit->field);
            assert_or_false(prev._multi_digit->digit_index == 0);
            assert_or_false(prev._seq_entry_index == _seq_entry_index);
            assert_or_false(prev._chip == _chip);
            assert_or_false(prev._channel == _channel);

            return true;
        }

        return false;
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

using doc_util::event_search::EventSearchMut;

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
        EventSearchMut kv{events};

        auto ev_begin = kv.beat_begin(time.beat);
        auto ev_end = kv.beat_end(time.beat);
        auto p = &subcolumn;

        for (auto it = ev_begin; it != ev_end; it++) {
            auto & event = it->v;

            if (std::get_if<subcolumns::Note>(p)) {
                event.note = {};
                event.instr = {};
                event.volume = {};
            } else
            if (std::get_if<subcolumns::Instrument>(p)) {
                event.instr = {};
            } else
            if (std::get_if<subcolumns::Volume>(p)) {
                event.volume = {};
            } else
            if (auto eff = std::get_if<subcolumns::EffectName>(p)) {
                // TODO v.effects[eff->effect_col]
                (void) eff;
            } else
            if (auto eff = std::get_if<subcolumns::EffectValue>(p)) {
                // TODO v.effects[eff->effect_col]
                (void) eff;
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
    doc::Note note,
    std::optional<doc::InstrumentIndex> instrument
) {
    // We don't need to check if the user is inserting "no note",
    // because it has type optional<Note> and value nullopt.

    // Copy event list.
    doc::EventList events =
        document.sequence[time.seq_entry_index].chip_channel_events[chip][channel];

    // Insert note.
    EventSearchMut kv{events};
    auto & ev = kv.get_or_insert(time.beat);

    ev.v.note = note;

    bool is_cut = note.is_cut() || note.is_release();

    if (is_cut) {
        ev.v.instr = {};
        // leave ev.v.volume as-is.
    } else {
        if (instrument) {
            ev.v.instr = *instrument;
        }
        // TODO if (volume) {}
    }

    return make_command(PatternEdit{
        time.seq_entry_index, chip, channel, std::move(events)
    });
}

// TODO add similar function for effect name (two ASCII characters, not nybbles).
std::tuple<uint8_t, EditBox> add_digit(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    PatternAndBeat time,
    MultiDigitField subcolumn,
    int digit_index,
    uint8_t nybble
) {
    // Copy event list.
    doc::EventList events =
        document.sequence[time.seq_entry_index].chip_channel_events[chip][channel];

    // Insert instrument.

    std::optional<uint8_t> & field = [&] () -> auto& {
        EventSearchMut kv{events};
        auto & ev = kv.get_or_insert(time.beat);

        if (std::holds_alternative<subcolumns::Instrument>(subcolumn)) {
            return ev.v.instr;
        }
        if (std::holds_alternative<subcolumns::Volume>(subcolumn)) {
            return ev.v.volume;
        }
        throw std::invalid_argument("Invalid subcolumn");
    }();

    if (digit_index == 0) {
        field = nybble;
    } else {
        // TODO non-fatal popup if assertion failed.
        assert(field.has_value());
        assert(field.value() < 0x10);

        // field is optional<u8> but make this a u32 to avoid promotion to signed int.
        uint32_t old_nybble = field.value_or(0);
        field = (old_nybble << 4u) | nybble;
    }

    return {
        *field,
        make_command(PatternEdit{
            time.seq_entry_index,
            chip,
            channel,
            std::move(events),
            MultiDigitEdit{subcolumn, digit_index},
        })
    };
}

// TODO write test to ensure subcolumn/selection deletion clears empty events.

}
