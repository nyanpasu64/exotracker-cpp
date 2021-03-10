#include "edit_pattern.h"
#include "edit_impl.h"
#include "edit/modified.h"
#include "timeline_iter.h"
#include "doc_util/event_search.h"
#include "util/release_assert.h"
#include "util/typeid_cast.h"

#include <cpp11-on-multicore/bitfield.h>

#include <utility>  // std::swap
#include <cassert>
#include <memory>  // make_unique. <memory> is already included for EditBox though.
#include <stdexcept>
#include <variant>

namespace edit::edit_pattern {

using namespace doc;
using timing::GridBlockBeat;
using edit_impl::make_command;

/// assert() only takes effect on debug builds.
/// On release builds, skip coalescing instead.
#define assert_or_false(EXPR) \
    assert((EXPR)); \
    if (!(EXPR)) return false


namespace edit {
    struct EditPattern {
        doc::Pattern pattern;
    };
    struct AddBlock {
        doc::TimelineBlock block;
    };
    struct RemoveBlock {};

    using Edit = std::variant<EditPattern, AddBlock, RemoveBlock>;
}

using edit::Edit;

/// Implements EditCommand. Other classes can store a vector of multiple PatternEdit.
struct PatternEdit {
    ChipIndex _chip;
    ChannelIndex _channel;
    GridIndex _grid_index;
    BlockIndex _block_index;

    Edit _edit;

    ModifiedFlags _modified = ModifiedFlags::Patterns;

    void apply_swap(doc::Document & document) {
        auto & doc_blocks =
            document.timeline[_grid_index]
            .chip_channel_cells[_chip][_channel]._raw_blocks;

        auto p = &_edit;

        if (auto edit = std::get_if<edit::EditPattern>(p)) {
            Pattern & doc_pattern = doc_blocks[_block_index].pattern;

            for (auto & ev : edit->pattern.events) {
                assert(ev.v != doc::RowEvent{});
                (void) ev;
            }
            std::swap(doc_pattern, edit->pattern);

        } else
        if (auto add = std::get_if<edit::AddBlock>(p)) {
            for (auto & ev : add->block.pattern.events) {
                assert(ev.v != doc::RowEvent{});
                (void) ev;
            }
            doc_blocks.insert(doc_blocks.begin() + _block_index, std::move(add->block));
            *p = edit::RemoveBlock{};

        } else
        if (std::get_if<edit::RemoveBlock>(p)) {
            *p = edit::AddBlock{std::move(doc_blocks[_block_index])};
            doc_blocks.erase(doc_blocks.begin() + _block_index);

        } else
            throw std::logic_error("PatternEdit with missing edit operaton");
    }

    bool can_coalesce(BaseEditCommand & /*prev*/) const {
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


struct EmptyBlock {
    /// Index where you can insert a new block filling a gap in time.
    /// time.block may point to the end of the array, out of bounds.
    GridBlockBeat time;

    /// Time the block can take up.
    BeatIndex begin_time;
    BeatOrEnd end_time;
};


[[nodiscard]] static
std::variant<GridBlockBeat, EmptyBlock> get_current_block(
    doc::Document const& document,
    doc::ChipIndex chip,
    doc::ChannelIndex channel,
    timing::GridAndBeat now
) {
    auto cell_ref = doc::TimelineChannelRef(document.timeline, chip, channel)[now.grid];
    doc::TimelineCell const& cell = cell_ref.cell;

    doc::PatternRef pattern_or_end = timeline_iter::pattern_or_end(cell_ref, now.beat);
    if (pattern_or_end.block < cell.size()) {
        doc::PatternRef pattern = pattern_or_end;
        // block_or_end() is required to return a block where block.end_time > beat.
        release_assert(now.beat < pattern.end_time);

        // Check if the beat points within the block.
        if (now.beat >= pattern.begin_time) {
            return GridBlockBeat{
                now.grid, pattern.block, now.beat - pattern.begin_time
            };
        }
    }

    // If now points in between blocks, compute the gap's boundaries.
    /// Index where you can insert a new block filling a gap in time.
    auto block_or_end = pattern_or_end.block;
    auto empty_begin = block_or_end.v > 0
        ? (BeatIndex) cell._raw_blocks[block_or_end - 1].end_time
        : 0;

    auto empty_end = block_or_end < cell.size()
        ? (BeatOrEnd) pattern_or_end.begin_time
        : END_OF_GRID;

    return EmptyBlock{
        {now.grid, block_or_end, now.beat - empty_begin}, empty_begin, empty_end
    };
}


using doc_util::event_search::EventSearchMut;

EditBox create_block(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    GridAndBeat abs_time
) {
    // We don't need to check if the user is inserting "no note",
    // because it has type optional<Note> and value nullopt.

    auto maybe_block = get_current_block(document, chip, channel, abs_time);
    auto p = &maybe_block;

    if (std::get_if<GridBlockBeat>(p)) {
        return make_command(edit_impl::NullEditCommand{});
    }

    auto empty = std::get_if<EmptyBlock>(p);
    release_assert(empty);
    GridBlockBeat time = empty->time;

    // Create new pattern.
    Edit edit = edit::AddBlock{doc::TimelineBlock{
        .begin_time = empty->begin_time,
        .end_time = empty->end_time,
        .pattern = Pattern{.events = EventList{}, .loop_length = {}},
    }};

    return make_command(PatternEdit{
        chip, channel, time.grid, time.block, std::move(edit)
    });
}

EditBox delete_cell(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    SubColumn subcolumn,
    GridAndBeat abs_time
) {
    auto maybe_block = get_current_block(document, chip, channel, abs_time);

    GridBlockBeat time;
    if (auto p_time = std::get_if<GridBlockBeat>(&maybe_block)) {
        time = *p_time;
    } else {
        // If you press Delete in a region with no block/pattern, do nothing.
        return make_command(edit_impl::NullEditCommand{});
    }

    // Copy pattern.
    doc::Pattern pattern =
        document.timeline[time.grid].chip_channel_cells[chip][channel]
        ._raw_blocks[time.block].pattern;

    // Erase certain event fields, based on where the cursor was positioned.
    {
        EventSearchMut kv{pattern.events};

        auto ev_begin = kv.beat_begin(time.beat);
        auto ev_end = kv.beat_end(time.beat);
        auto p = &subcolumn;

        for (auto it = ev_begin; it != ev_end; it++) {
            auto & event = it->v;

            if (std::get_if<SubColumn_::Note>(p)) {
                event.note = {};
                event.instr = {};
                event.volume = {};
            } else
            if (std::get_if<SubColumn_::Instrument>(p)) {
                event.instr = {};
            } else
            if (std::get_if<SubColumn_::Volume>(p)) {
                event.volume = {};
            } else
            if (auto eff = std::get_if<SubColumn_::Effect>(p)) {
                event.effects[eff->effect_col] = {};
            } else
                throw std::invalid_argument("Invalid subcolumn passed to delete_cell()");
        }
    }

    // If we erase all fields from an event, remove the event entirely.
    erase_empty(pattern.events);

    return make_command(PatternEdit{
        chip, channel, time.grid, time.block, edit::EditPattern{std::move(pattern)}
    });
}

EditBox insert_note(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    GridAndBeat abs_time,
    doc::Note note,
    std::optional<doc::InstrumentIndex> instrument
) {
    // We don't need to check if the user is inserting "no note",
    // because it has type optional<Note> and value nullopt.

    auto maybe_block = get_current_block(document, chip, channel, abs_time);
    auto p = &maybe_block;

    GridBlockBeat time;
    Edit edit;
    doc::EventList * events;

    if (auto exists = std::get_if<GridBlockBeat>(p)) {
        time = *exists;

        // Copy pattern.
        edit = edit::EditPattern{doc::Pattern(
            document.timeline[time.grid].chip_channel_cells[chip][channel]
                ._raw_blocks[time.block].pattern
        )};
        events = &std::get<edit::EditPattern>(edit).pattern.events;

    } else
    if (auto empty = std::get_if<EmptyBlock>(p)) {
        time = empty->time;

        // Create new pattern.
        edit = edit::AddBlock{doc::TimelineBlock{
            .begin_time = empty->begin_time,
            .end_time = empty->end_time,
            .pattern = Pattern{.events = EventList{}, .loop_length = {}},
        }};
        events = &std::get<edit::AddBlock>(edit).block.pattern.events;

    } else
        throw std::logic_error("insert_note() get_current_block() returned nothing");

    // Insert note.
    EventSearchMut kv{*events};
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
        // TODO add ability to set volume of each new note
    }

    return make_command(PatternEdit{
        chip, channel, time.grid, time.block, std::move(edit)
    });
}

BEGIN_BITFIELD_TYPE(HexByte, uint8_t)
//  ADD_BITFIELD_MEMBER(memberName, offset, bits)
    ADD_BITFIELD_MEMBER(lower,      0,      4)
    ADD_BITFIELD_MEMBER(upper,      4,      4)
END_BITFIELD_TYPE()

// TODO add similar function for effect name (two ASCII characters, not nybbles).
std::tuple<uint8_t, EditBox> add_digit(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    GridAndBeat abs_time,
    MultiDigitField subcolumn,
    DigitAction digit_action,
    uint8_t nybble)
{
    auto maybe_block = get_current_block(document, chip, channel, abs_time);
    auto p = &maybe_block;

    GridBlockBeat time;
    Edit edit;
    doc::EventList * events;  // events: 'edit

    if (auto exists = std::get_if<GridBlockBeat>(p)) {
        time = *exists;

        // Copy pattern.
        edit = edit::EditPattern{doc::Pattern(
            document.timeline[time.grid].chip_channel_cells[chip][channel]
                ._raw_blocks[time.block].pattern
        )};
        events = &std::get<edit::EditPattern>(edit).pattern.events;

    } else
    if (auto empty = std::get_if<EmptyBlock>(p)) {
        time = empty->time;

        // Create new pattern.
        edit = edit::AddBlock{doc::TimelineBlock{
            .begin_time = empty->begin_time,
            .end_time = empty->end_time,
            .pattern = Pattern{.events = EventList{}, .loop_length = {}},
        }};
        events = &std::get<edit::AddBlock>(edit).block.pattern.events;

    } else
        throw std::logic_error("add_digit() get_current_block() returned nothing");

    // Insert instrument/volume, edit effect. No-op if no effect present.

    auto make_some = [](std::optional<uint8_t> & maybe_field) -> uint8_t & {
        maybe_field = maybe_field.value_or(0);
        return *maybe_field;
    };

    // field: ('events = 'edit).
    auto field = [&] () -> uint8_t * {
        EventSearchMut kv{*events};


        if (std::holds_alternative<SubColumn_::Instrument>(subcolumn)) {
            auto & ev = kv.get_or_insert(time.beat);
            return &make_some(ev.v.instr);
        }
        if (std::holds_alternative<SubColumn_::Volume>(subcolumn)) {
            auto & ev = kv.get_or_insert(time.beat);
            return &make_some(ev.v.volume);
        }
        if (auto p = std::get_if<SubColumn_::Effect>(&subcolumn)) {
            auto ev = kv.get_maybe(time.beat);
            // If there's no event at the current time,
            // discard the new value and don't modify the document.
            if (!ev) {
                return nullptr;
            }

            auto & eff = ev->v.effects[p->effect_col];
            // If we're editing an empty effect slot,
            // discard the new value and don't modify the document.
            if (!eff) {
                return nullptr;
            }

            // If we're editing an (effect, value) pair, return the value.
            return &eff->value;
        }
        throw std::invalid_argument("Invalid subcolumn passed to add_digit()");
    }();

    HexByte value = field ? *field : 0;

    switch (digit_action) {
    case DigitAction::Replace:
        value = nybble;
        break;

    case DigitAction::ShiftLeft:
        value.upper = value.lower;
        value.lower = nybble;
        break;

    case DigitAction::UpperNybble:
        value.upper = nybble;
        break;

    case DigitAction::LowerNybble:
        value.lower = nybble;
        break;

    default:
        throw std::logic_error("invalid DigitAction when calling add_digit()");
    }

    if (field) {
        // Mutates `edit`.
        *field = value;
    }

    return {
        // Tell GUI the newly selected volume/instrument number.
        value,
        // Return edit to be applied to GUI/audio documents.
        make_command(PatternEdit{chip, channel, time.grid, time.block, std::move(edit)}),
    };
}

// TODO write test to ensure subcolumn/selection deletion clears empty events.

[[nodiscard]] EditBox add_effect_char(
    Document const& document,
    ChipIndex chip,
    ChannelIndex channel,
    GridAndBeat abs_time,
    SubColumn_::Effect subcolumn,
    EffectAction effect_action)
{
    auto maybe_block = get_current_block(document, chip, channel, abs_time);
    auto p = &maybe_block;

    GridBlockBeat time;
    Edit edit;
    doc::EventList * events;  // events: 'edit

    if (auto exists = std::get_if<GridBlockBeat>(p)) {
        time = *exists;

        // Copy pattern.
        edit = edit::EditPattern{doc::Pattern(
            document.timeline[time.grid].chip_channel_cells[chip][channel]
                ._raw_blocks[time.block].pattern
        )};
        events = &std::get<edit::EditPattern>(edit).pattern.events;

    } else
    if (auto empty = std::get_if<EmptyBlock>(p)) {
        time = empty->time;

        // Create new pattern.
        edit = edit::AddBlock{doc::TimelineBlock{
            .begin_time = empty->begin_time,
            .end_time = empty->end_time,
            .pattern = Pattern{.events = EventList{}, .loop_length = {}},
        }};
        events = &std::get<edit::AddBlock>(edit).block.pattern.events;

    } else
        throw std::logic_error("add_effect_char() get_current_block() returned nothing");

    // field: ('events = 'edit).
    auto & field = [&] () -> doc::Effect & {
        EventSearchMut kv{*events};
        auto & ev = kv.get_or_insert(time.beat);

        doc::MaybeEffect & maybe_eff = ev.v.effects[subcolumn.effect_col];
        maybe_eff = maybe_eff.value_or(Effect());
        return *maybe_eff;
    }();

    auto ep = &effect_action;
    if (auto p = std::get_if<EffectAction_::Replace>(ep)) {
        field.name = p->name;
    } else
    if (auto p = std::get_if<EffectAction_::LeftChar>(ep)) {
        field.name[0] = p->c;
    } else
    if (auto p = std::get_if<EffectAction_::RightChar>(ep)) {
        field.name[1] = p->c;
    } else
        throw std::logic_error("invalid EffectAction when calling add_effect_char()");

    return make_command(PatternEdit{
        chip, channel, time.grid, time.block, std::move(edit)
    });
}

}
