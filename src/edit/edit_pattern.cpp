#include "edit_pattern.h"
#include "edit_impl.h"
#include "doc_util/time_util.h"
#include "doc_util/track_util.h"
#include "doc_util/event_search.h"
#include "util/compare.h"
#include "util/compare_impl.h"
#include "util/expr.h"
#include "util/release_assert.h"
#include "util/typeid_cast.h"
#include "util/variant_cast.h"

#include <cpp11-on-multicore/bitfield.h>

#include <utility>  // std::swap
#include <cassert>
#include <memory>  // make_unique. <memory> is already included for EditBox though.
#include <stdexcept>
#include <variant>

namespace edit::edit_pattern {

using namespace doc;
using timing::TickT;
using namespace edit_impl;

namespace edit {
    struct EditPattern {
        doc::Pattern pattern;
    };
    struct AddBlock {
        doc::TrackBlock block;
    };
    struct RemoveBlock {};

    using Edit = std::variant<EditPattern, AddBlock, RemoveBlock>;
}

using edit::Edit;

/// Implements EditCommand. Other classes can store a vector of multiple PatternEdit.
struct PatternEdit {
    ChipIndex _chip;
    ChannelIndex _channel;
    BlockIndex _block;

    Edit _edit;

    ModifiedFlags _modified = ModifiedFlags::Patterns;

    void apply_swap(doc::Document & document) {
        auto & doc_blocks = document.sequence[_chip][_channel].blocks;

        auto p = &_edit;

        if (auto edit = std::get_if<edit::EditPattern>(p)) {
            Pattern & doc_pattern = doc_blocks[_block].pattern;

            // Reject all edits that create 64k or more events in a single edit.
            // Don't assert that this never happens,
            // because a user can perform this through valid inputs only.
            if (edit->pattern.events.size() > MAX_EVENTS_PER_PATTERN) {
                return;
            }
            // Why do we check for too many events at apply_swap time, but too many
            // blocks at create time?

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
            doc_blocks.insert(doc_blocks.begin() + _block, std::move(add->block));
            *p = edit::RemoveBlock{};

        } else
        if (std::get_if<edit::RemoveBlock>(p)) {
            *p = edit::AddBlock{std::move(doc_blocks[_block])};
            doc_blocks.erase(doc_blocks.begin() + _block);

        } else
            throw std::logic_error("PatternEdit with missing edit operaton");

#ifdef DEBUG
        TickT prev_end = 0;
        for (auto const& block : doc_blocks) {
            assert(block.begin_tick >= prev_end);
            assert(block.loop_count > 0);
            assert(block.pattern.length_ticks > 0);
            prev_end =
                block.begin_tick + (int) block.loop_count * block.pattern.length_ticks;
        }
#endif
    }

    using Impl = ImplEditCommand<PatternEdit, Override::None>;
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


using doc_util::time_util::MeasureIter;
using doc_util::time_util::MeasureIterResult;
using doc_util::track_util::TrackPatternIterRef;
using doc_util::track_util::IterResultRef;
using doc_util::event_search::EventSearchMut;

EditBox delete_cell(
    Document const& document,
    const ChipIndex chip,
    const ChannelIndex channel,
    const SubColumn subcolumn,
    const TickT now
) {
    doc::SequenceTrackRef track = document.sequence[chip][channel];

    const IterResultRef x = TrackPatternIterRef::at_time(track, now);
    if (x.snapped_later) {
        // If you press Delete in a region with no block/pattern, do nothing.
        return make_command(edit_impl::NullEditCommand{});
    }

    // If you pressed Delete in a block/pattern...
    const PatternRef pattern_ref = x.iter.peek().value();
    const TickT rel_tick = now - pattern_ref.begin_tick;
    assert(rel_tick >= 0);

    // Copy pattern.
    doc::Pattern pattern =
        document.sequence[chip][channel].blocks[pattern_ref.block].pattern;

    // Erase certain event fields, based on where the cursor was positioned.
    {
        EventSearchMut kv{pattern.events};

        // TODO erase a full row of events?
        auto ev_begin = kv.tick_begin(rel_tick);
        auto ev_end = kv.tick_end(rel_tick);
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

    return make_command(PatternEdit {
        ._chip = chip,
        ._channel = channel,
        ._block = pattern_ref.block,
        ._edit = edit::EditPattern{std::move(pattern)},
    });
}

static edit::AddBlock create_block_at(TickT block_begin, TickT block_end) {
    assert(block_begin < block_end);

    return edit::AddBlock{doc::TrackBlock {
        .begin_tick = block_begin,
        .loop_count = 1,
        .pattern = Pattern {
            .length_ticks = block_end - block_begin,
            .events = EventList{},
        },
    }};
}

struct CreateOrEdit {
    ChipIndex chip;
    ChannelIndex channel;
    BlockIndex block;
    std::variant<edit::EditPattern, edit::AddBlock> edit;
    TickT rel_tick;

    /// Returns nullopt if we need to create a block, but MAX_BLOCKS_PER_TRACK blocks
    /// already exist.
    static std::optional<CreateOrEdit> try_make(
        doc::Document const& doc,
        ChipIndex chip,
        ChannelIndex channel,
        TickT now,
        ExtendBlock block_mode)
    {
        doc::SequenceTrackRef track = doc.sequence[chip][channel];

        const IterResultRef p = TrackPatternIterRef::at_time(track, now);
        if (!p.snapped_later) {
            // When editing a block/pattern, return a pattern copy in-place.
            const PatternRef pattern_ref = p.iter.peek().value();
            const TickT rel_tick = now - pattern_ref.begin_tick;
            assert(rel_tick >= 0);

            // Copy pattern.
            return CreateOrEdit {
                .chip = chip,
                .channel = channel,
                .block = pattern_ref.block,
                .edit = edit::EditPattern{doc::Pattern(
                    doc.sequence[chip][channel].blocks[pattern_ref.block].pattern
                )},
                .rel_tick = rel_tick,
            };
        }
        // When editing between blocks/patterns...

        // nullopt if inserting notes before the first block.
        const MaybePatternRef above = EXPR(
            auto prev = p.iter;
            prev.prev();
            return prev.peek();
        );
        // nullopt if inserting notes after the last block.
        const MaybePatternRef below = p.iter.peek();

        // Measure boundaries, truncated to fit within above/below patterns.
        TickT measure_begin, measure_end;
        {
            // Returns the nearest measure boundary <= now.
            const MeasureIter curr_meas = MeasureIter::at_time(doc, now).iter;
            MeasureIter next_meas = curr_meas;
            next_meas.next();

            measure_begin = curr_meas.peek();
            measure_end = next_meas.peek();

            if (above) {
                measure_begin = std::max(measure_begin, above->end_tick);
            }
            if (below) {
                measure_end = std::min(measure_end, below->begin_tick);
            }
            release_assert(measure_begin < measure_end);
            release_assert(now >= measure_begin);
        }

        auto create_block_index = below
            ? below->block
            : (BlockIndex) track.blocks.size();

        // Do not capture measure_begin!
        auto extend_above = [&doc, now, &above, chip, channel, measure_end]() {
            release_assert(above);

            TrackBlock const& block =
                doc.sequence[chip][channel].blocks[above->block];
            release_assert(block.loop_count == 1);

            const TickT block_begin = block.begin_tick;
            release_assert(measure_end > block_begin);

            // Copy pattern and extend to measure_end.
            auto pattern = block.pattern;
            pattern.length_ticks = measure_end - block_begin;
            return CreateOrEdit {
                .chip = chip,
                .channel = channel,
                .block = above->block,
                .edit = edit::EditPattern{std::move(pattern)},
                .rel_tick = now - block_begin,
            };
        };

        // Do not capture measure_begin!
        auto try_create_block = [
            now, chip, channel, create_block_index, measure_end, &track
        ](TickT block_begin) -> std::optional<CreateOrEdit> {
            if (track.blocks.size() >= MAX_BLOCKS_PER_TRACK) {
                return {};
            }
            return CreateOrEdit {
                .chip = chip,
                .channel = channel,
                .block = create_block_index,
                .edit = create_block_at(block_begin, measure_end),
                .rel_tick = now - block_begin,
            };
        };

        // Since we're in a gap between blocks, the above pattern is a block end. If
        // it's a block begin as well, the block has a loop count of 1.
        const bool above_unlooped = above && above->is_block_begin;
        // above->is_block_begin is UB if above is nullopt, but we never use it then...

        switch (block_mode) {
        case ExtendBlock::Never:
            // Create a 1-measure block (truncated to fit).
            return try_create_block(measure_begin);

        case ExtendBlock::Adjacent:
            // If next to above unlooped pattern, extend it.
            if (above && above_unlooped && above->end_tick == measure_begin) {
                return extend_above();
            } else {
                // Otherwise, create a 1-measure block (truncated to fit).
                return try_create_block(measure_begin);
            }

        case ExtendBlock::Always:
            // If above pattern exists...
            if (above) {
                // Extend if unlooped, create maximally sized block otherwise.
                if (above_unlooped) {
                    return extend_above();
                } else {
                    return try_create_block(above->end_tick);
                }
            } else {
                // Otherwise (if creating first pattern), start from time=0.
                return try_create_block(0);
            }
        }
        throw std::logic_error(
            "CreateOrEdit::make() passed unrecognized ExtendBlock mode"
        );
    }

    Pattern & pattern() {
        if (auto edit_ = std::get_if<edit::EditPattern>(&edit)) {
            return edit_->pattern;
        } else
        if (auto add = std::get_if<edit::AddBlock>(&edit)) {
            return add->block.pattern;
        } else
            throw std::logic_error("CreateOrEdit pattern(), edit holds nothing");
    }

    PatternEdit into_edit() && {
        return PatternEdit {
            ._chip = chip,
            ._channel = channel,
            ._block = block,
            ._edit = variant_cast(std::move(edit)),
        };
    }
};

EditBox insert_note(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    TickT now,
    ExtendBlock block_mode,
    doc::Note note,
    std::optional<doc::InstrumentIndex> instrument)
{
    // We don't need to check if `Note note` contains "no note",
    // because "no note" has type optional<Note> and value nullopt.

    auto maybe_edit = CreateOrEdit::try_make(document, chip, channel, now, block_mode);
    if (!maybe_edit) {
        // Return an edit command to simplify the call site and move the cursor anyway.
        return make_command(NullEditCommand{});
    }
    CreateOrEdit & edit = *maybe_edit;

    EventList & pattern_events = edit.pattern().events;

    // Insert note.
    EventSearchMut kv{pattern_events};
    auto & ev = kv.get_or_insert(edit.rel_tick);

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

    return make_command(std::move(edit).into_edit());
}

BEGIN_BITFIELD_TYPE(HexByte, uint8_t)
//  ADD_BITFIELD_MEMBER(memberName, offset, bits)
    ADD_BITFIELD_MEMBER(lower,      0,      4)
    ADD_BITFIELD_MEMBER(upper,      4,      4)
END_BITFIELD_TYPE()

std::tuple<uint8_t, EditBox> add_digit(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    TickT now,
    ExtendBlock block_mode,
    MultiDigitField subcolumn,
    DigitAction digit_action,
    uint8_t nybble)
{
    auto maybe_edit = CreateOrEdit::try_make(document, chip, channel, now, block_mode);
    if (!maybe_edit) {
        // The returned uint8_t doesn't really matter, it's not worth the complexity of
        // rewriting the remaining function around maybe_edit.
        return {0, make_command(NullEditCommand{})};
    }
    CreateOrEdit & edit = *maybe_edit;

    EventList & pattern_events = edit.pattern().events;

    // Insert instrument/volume, edit effect. No-op if no effect present.

    auto make_some = [](std::optional<uint8_t> & maybe_field) -> uint8_t & {
        maybe_field = maybe_field.value_or(0);
        return *maybe_field;
    };

    // field: ('events = 'edit).
    auto field = [&] () -> uint8_t * {
        EventSearchMut kv{pattern_events};


        if (std::holds_alternative<SubColumn_::Instrument>(subcolumn)) {
            auto & ev = kv.get_or_insert(edit.rel_tick);
            return &make_some(ev.v.instr);
        }
        if (std::holds_alternative<SubColumn_::Volume>(subcolumn)) {
            auto & ev = kv.get_or_insert(edit.rel_tick);
            return &make_some(ev.v.volume);
        }
        if (auto p = std::get_if<SubColumn_::Effect>(&subcolumn)) {
            auto ev = kv.get_maybe(edit.rel_tick);
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
        make_command(std::move(edit).into_edit()),
    };
}

// TODO write test to ensure subcolumn/selection deletion clears empty events.

[[nodiscard]] EditBox add_effect_char(
    Document const& document,
    ChipIndex chip,
    ChannelIndex channel,
    TickT now,
    ExtendBlock block_mode,
    SubColumn_::Effect subcolumn,
    EffectAction effect_action)
{
    auto maybe_edit = CreateOrEdit::try_make(document, chip, channel, now, block_mode);
    if (!maybe_edit) {
        return make_command(NullEditCommand{});
    }
    CreateOrEdit & edit = *maybe_edit;

    EventList & pattern_events = edit.pattern().events;

    // field: ('events = 'edit).
    auto & field = [&] () -> doc::Effect & {
        EventSearchMut kv{pattern_events};
        auto & ev = kv.get_or_insert(edit.rel_tick);

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

    return make_command(std::move(edit).into_edit());
}

}
