#include "pattern.h"
#include "edit_impl.h"
#include "edit_util/kv.h"
#include "util/typeid_cast.h"

#include <algorithm>  // std::swap
#include <cassert>
#include <memory>  // make_unique. <memory> is already included for EditBox though.

namespace edit::pattern {

using namespace doc;
using edit_impl::make_command;

enum class PatternEditType {
    Other,
    InstrumentDigit1,
    InstrumentDigit2,
};
using Type = PatternEditType;

/// Implements EditCommand. Other classes can store a vector of multiple PatternEdit.
struct PatternEdit {
    SeqEntryIndex _seq_entry_index;
    ChipIndex _chip;
    ChannelIndex _channel;

    doc::EventList _events;

    PatternEditType _type = Type::Other;

    void apply_swap(doc::Document & document) {
        // TODO #ifndef NDEBUG, assert all of _events are non-empty.
        auto & doc_events =
            document.sequence[_seq_entry_index].chip_channel_events[_chip][_channel];
        doc_events.swap(_events);
    }

    bool can_coalesce(BaseEditCommand & prev) const {
        switch (_type) {

        case Type::InstrumentDigit2:
            if (auto p = typeid_cast<edit_impl::ImplEditCommand<PatternEdit> *>(&prev)) {
                auto & prev = p->_body;
                if (prev._type == Type::InstrumentDigit1) {
                    assert(_seq_entry_index == prev._seq_entry_index);
                    assert(_chip == prev._chip);
                    assert(_channel == prev._channel);
                    return _seq_entry_index == prev._seq_entry_index
                        && _chip == prev._chip
                        && _channel == prev._channel;
                }
            }
            return false;

        default:
            return false;
        }
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
    edit_util::kv::KV kv{events};
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

// TODO Add function_ref<uint8_t & (RowEvent&)> parameter
// to reuse logic for volume and effect value.
// TODO add similar function for effect name (two ASCII characters, not nybbles).
EditBox instrument_digit_1(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    PatternAndBeat time,
    uint8_t nybble
) {
    // Copy event list.
    doc::EventList events =
        document.sequence[time.seq_entry_index].chip_channel_events[chip][channel];

    // Insert instrument.
    edit_util::kv::KV kv{events};
    auto & ev = kv.get_or_insert(time.beat);

    ev.v.instr = nybble;

    return make_command(PatternEdit{
        time.seq_entry_index,
        chip,
        channel,
        std::move(events),
        Type::InstrumentDigit1,
    });
}

EditBox instrument_digit_2(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    PatternAndBeat time,
    uint8_t nybble
) {
    // Copy event list.
    doc::EventList events =
        document.sequence[time.seq_entry_index].chip_channel_events[chip][channel];

    // Insert instrument.
    edit_util::kv::KV kv{events};
    auto & ev = kv.get_or_insert(time.beat);

    // TODO non-fatal popup if assertion failed.
    assert(ev.v.instr.has_value());
    assert(ev.v.instr.value() < 0x10);

    uint8_t old_nybble = ev.v.instr.value_or(0);
    ev.v.instr = (old_nybble << 4) | nybble;

    return make_command(PatternEdit{
        time.seq_entry_index,
        chip,
        channel,
        std::move(events),
        Type::InstrumentDigit2,
    });
}

// TODO write test to ensure subcolumn/selection deletion clears empty events.

}
