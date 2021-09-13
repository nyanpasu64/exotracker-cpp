#include "gui/history.h"
#include "gui/cursor.h"
#include "edit/edit_pattern.h"
#include "doc.h"
#include "chip_common.h"
#include "timing_common.h"
#include "sample_docs.h"

#include <functional>

#include <doctest.h>
#include "test_utils/parameterize.h"

// internal linkage
namespace {

using gui::history::History;
using gui::history::CursorEdit;
using gui::cursor::Cursor;
using edit::EditBox;
using timing::GridAndBeat;

[[nodiscard]] doc::TimelineCell const& get_cell(doc::Document const& d) {
    // grid 0, chip 0, channel 0
    return d.timeline[0].chip_channel_cells[0][0];
}

using GetEdit = edit::EditBox (*)(doc::Document const&);
namespace ep = edit::edit_pattern;

/// When we switched to per-digit cursors (and an unused OpenMPT-style digit mode),
/// we eliminated merging two adjacent edits to the same subcolumn.
/// This allowed removing a significant amount of code.
///
/// Applying edits a and b on a document (which may/not have an existing block),
/// assert that merging does not occur.
void test_pattern_edits(bool start_with_block, GetEdit a, GetEdit b) {
    auto h = History(sample_docs::DOCUMENTS.at("empty").clone());

    if (start_with_block) {
        // Create a block, so both a and b operate on an existing block.
        edit::EditBox create_block =
            ep::create_block(h.get_document(), 0, 0, GridAndBeat{0, 0});
        h.push(CursorEdit{std::move(create_block), Cursor{}, Cursor{}});
    }

    auto begin_doc = get_cell(h.get_document().clone());

    // Push first edit.
    h.push(CursorEdit{a(h.get_document()), Cursor{}, Cursor{}});
    auto after_a = get_cell(h.get_document().clone());
    CHECK_UNARY(after_a != begin_doc);

    // Push second edit.
    h.push(CursorEdit{b(h.get_document()), Cursor{}, Cursor{}});
    auto after_b = get_cell(h.get_document().clone());
    CHECK_UNARY(after_b != begin_doc);
    // after_b may/not equal after_a.

    // Undo and check if both edits were reverted.
    h.undo();
    auto undo = get_cell(h.get_document().clone());
    CHECK_UNARY(undo == after_a);
    CHECK_UNARY(undo != begin_doc);
}

namespace sc = edit::edit_pattern::SubColumn_;
using chip_common::ChipIndex;
using chip_common::ChannelIndex;

using DA = ep::DigitAction;

inline EditBox add_digit_simple(
    doc::Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    GridAndBeat time,
    ep::MultiDigitField subcolumn,
    ep::DigitAction digit_action,
    uint8_t nybble
) {
    auto [_value, box] = ep::add_digit(
        document, chip, channel, time, subcolumn, digit_action, nybble
    );
    return std::move(box);
}


EditBox volume_write_1(doc::Document const& d) {
    return add_digit_simple(d, 0, 0, GridAndBeat{0, 0}, sc::Volume{}, DA::Replace, 0x1);
}

EditBox volume_write_2(doc::Document const& d) {
    return add_digit_simple(d, 0, 0, GridAndBeat{0, 0}, sc::Volume{}, DA::Replace, 0x11);
}

EditBox volume_shift(doc::Document const& d) {
    return add_digit_simple(d, 0, 0, GridAndBeat{0, 0}, sc::Volume{}, DA::ShiftLeft, 0x2);
}

EditBox instr_write(doc::Document const& d) {
    return add_digit_simple(d, 0, 0, GridAndBeat{0, 0}, sc::Instrument{}, DA::Replace, 0x11);
}

EditBox instr_shift(doc::Document const& d) {
    return add_digit_simple(d, 0, 0, GridAndBeat{0, 0}, sc::Instrument{}, DA::ShiftLeft, 0x2);
}

PARAMETERIZE(should_start_with_block, bool, start_with_block,
    OPTION(start_with_block, false);
    OPTION(start_with_block, true);
)


TEST_CASE("Check that volume editing operations are not merged") {
    bool start_with_block;
    PICK(should_start_with_block(start_with_block));
    test_pattern_edits(start_with_block, volume_write_1, volume_write_2);
    test_pattern_edits(start_with_block, volume_write_1, volume_shift);
    test_pattern_edits(start_with_block, volume_write_2, volume_shift);
    test_pattern_edits(start_with_block, volume_shift, volume_shift);
}

TEST_CASE("Check that mixing volume/instrument edits are not merged") {
    bool start_with_block;
    PICK(should_start_with_block(start_with_block));
    test_pattern_edits(start_with_block, volume_write_1, instr_write);
}

TEST_CASE("Check that instrument edits are not merged") {
    bool start_with_block;
    PICK(should_start_with_block(start_with_block));
    test_pattern_edits(start_with_block, instr_write, instr_write);
    test_pattern_edits(start_with_block, instr_write, instr_shift);
    test_pattern_edits(start_with_block, instr_shift, instr_shift);
}

}
