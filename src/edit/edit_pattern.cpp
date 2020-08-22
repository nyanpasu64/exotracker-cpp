#include "edit_pattern.h"
#include "edit_impl.h"
#include "edit/modified.h"
#include "timeline_iter.h"
#include "doc_util/event_search.h"
#include "util/release_assert.h"
#include "util/typeid_cast.h"

#include <algorithm>  // std::swap
#include <cassert>
#include <memory>  // make_unique. <memory> is already included for EditBox though.
#include <stdexcept>
#include <variant>

namespace edit::edit_pattern {

using namespace doc;
using timing::GridBlockBeat;
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

    std::optional<MultiDigitEdit> _multi_digit{};
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
            assert_or_false(prev._chip == _chip);
            assert_or_false(prev._channel == _channel);
            assert_or_false(prev._grid_index == _grid_index);
            assert_or_false(prev._block_index == _block_index);

            // If we're entering the second digit, the previous operation must be
            // "enter first digit", which is AddBlock or EditPattern.
            // When AddBlock is applied to a document, it turns into RemoveBlock.
            // When EditPattern is applied to a document, it remains as-is.
            assert_or_false(
                std::holds_alternative<edit::RemoveBlock>(prev._edit) ||
                std::holds_alternative<edit::EditPattern>(prev._edit)
            );

            // The current operation must be "enter second digit", which is EditPattern.
            // And History calls can_coalesce() *after* apply_swap().
            // But it remains as-is.
            assert_or_false(std::holds_alternative<edit::EditPattern>(_edit));

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

// TODO add similar function for effect name (two ASCII characters, not nybbles).
std::tuple<uint8_t, EditBox> add_digit(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    GridAndBeat abs_time,
    MultiDigitField subcolumn,
    int digit_index,
    uint8_t nybble
) {
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

    // Insert instrument.

    std::optional<uint8_t> & field = [&] () -> auto& {
        EventSearchMut kv{*events};
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
            chip,
            channel,
            time.grid,
            time.block,
            std::move(edit),
            MultiDigitEdit{subcolumn, digit_index},
        })
    };
}

// TODO write test to ensure subcolumn/selection deletion clears empty events.

}
