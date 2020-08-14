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
    // chip 0, channel 0, grid 0
    return d.chip_channel_timelines[0][0][0];
}

using GetEdit = edit::EditBox (*)(doc::Document const&);
namespace ep = edit::edit_pattern;

/// Running each of a and b on a document (which may/not have an existing block),
/// assert coalescing occurs or not.
void test_pattern_edits(
    bool start_with_block, GetEdit a, GetEdit b, bool should_coalesce
) {
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
    CHECK_UNARY(after_b != after_a);

    // Undo and check if both edits were reverted.
    h.undo();
    auto undo = get_cell(h.get_document().clone());
    if (should_coalesce) {
        CHECK_UNARY(undo == begin_doc);
        CHECK_UNARY(undo != after_a);
    } else {
        CHECK_UNARY(undo == after_a);
        CHECK_UNARY(undo != begin_doc);
    }
}

namespace sc = edit::edit_pattern::subcolumns;
using chip_common::ChipIndex;
using chip_common::ChannelIndex;

inline EditBox add_digit_simple(
    doc::Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    GridAndBeat time,
    edit::edit_pattern::MultiDigitField subcolumn,
    int digit_index,
    uint8_t nybble
) {
    auto [_value, box] = ep::add_digit(
        document, chip, channel, time, subcolumn, digit_index, nybble
    );
    return std::move(box);
}


EditBox volume_0(doc::Document const& d) {
    return add_digit_simple(d, 0, 0, GridAndBeat{0, 0}, sc::Volume{}, 0, 0x1);
}

EditBox volume_0_alt(doc::Document const& d) {
    return add_digit_simple(d, 0, 0, GridAndBeat{0, 0}, sc::Volume{}, 0, 0x3);
}

EditBox volume_1(doc::Document const& d) {
    return add_digit_simple(d, 0, 0, GridAndBeat{0, 0}, sc::Volume{}, 1, 0x2);
}

EditBox instr_0(doc::Document const& d) {
    return add_digit_simple(d, 0, 0, GridAndBeat{0, 0}, sc::Instrument{}, 0, 0x1);
}

EditBox instr_1(doc::Document const& d) {
    return add_digit_simple(d, 0, 0, GridAndBeat{0, 0}, sc::Instrument{}, 1, 0x2);
}

PARAMETERIZE(should_start_with_block, bool, start_with_block,
    OPTION(start_with_block, false);
    OPTION(start_with_block, true);
)


TEST_CASE("Check that volume editing operations are coalesced") {
    bool start_with_block;
    PICK(should_start_with_block(start_with_block));
    test_pattern_edits(start_with_block, volume_0, volume_1, true);
}

TEST_CASE("Check that 'first digit' volume edits are not coalesced") {
    bool start_with_block;
    PICK(should_start_with_block(start_with_block));
    test_pattern_edits(start_with_block, volume_0, volume_0_alt, false);
}

// The GUI is intended to make it impossible to enter a "volume digit 1"
// except for right after "volume digit 0", without moving the cursor.
// So it's not worth unit-testing volume 1 and 0 located at different spots.

TEST_CASE("Check that mixing volume/instrument edits are not coalesced") {
    bool start_with_block;
    PICK(should_start_with_block(start_with_block));
    test_pattern_edits(start_with_block, volume_0, instr_0, false);
}

TEST_CASE("Check that instrument edits are coalesced") {
    bool start_with_block;
    PICK(should_start_with_block(start_with_block));
    test_pattern_edits(start_with_block, instr_0, instr_1, true);
}

TEST_CASE("Check that 'first digit' instrument edits are coalesced") {
    bool start_with_block;
    PICK(should_start_with_block(start_with_block));
    test_pattern_edits(start_with_block, instr_0, instr_1, true);
}

}
