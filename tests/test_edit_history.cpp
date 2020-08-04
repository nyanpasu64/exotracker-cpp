#include "gui/history.h"
#include "gui/cursor.h"
#include "edit/edit_pattern.h"
#include "doc.h"
#include "chip_common.h"
#include "timing_common.h"
#include "sample_docs.h"

#include <doctest.h>

#include <functional>

// internal linkage
namespace {

using gui::history::History;
using gui::history::CursorEdit;
using gui::cursor::Cursor;
using edit::EditBox;
using timing::PatternAndBeat;

[[nodiscard]] doc::EventList const& get_sequence(doc::Document const& d) {
    return d.sequence[0].chip_channel_events[0][0];
}

using EditIt = edit::EditBox (*)(doc::Document const&);

/// Assert coalescing occurs or not.
void test_pattern_edits(EditIt a, EditIt b, bool should_coalesce) {
    auto h = History(sample_docs::DOCUMENTS.at("empty").clone());
    auto begin_doc = get_sequence(h.get_document().clone());

    // Push first edit.
    h.push(CursorEdit{a(h.get_document()), Cursor{}, Cursor{}});
    auto after_a = get_sequence(h.get_document().clone());
    CHECK_UNARY(after_a != begin_doc);

    // Push second edit.
    h.push(CursorEdit{b(h.get_document()), Cursor{}, Cursor{}});
    auto after_b = get_sequence(h.get_document().clone());
    CHECK_UNARY(after_b != begin_doc);
    CHECK_UNARY(after_b != after_a);

    // Undo and check if both edits were reverted.
    h.undo();
    auto undo = get_sequence(h.get_document().clone());
    if (should_coalesce) {
        CHECK_UNARY(undo == begin_doc);
        CHECK_UNARY(undo != after_a);
    } else {
        CHECK_UNARY(undo == after_a);
        CHECK_UNARY(undo != begin_doc);
    }
}

namespace ep = edit::edit_pattern;
namespace sc = edit::edit_pattern::subcolumns;
using chip_common::ChipIndex;
using chip_common::ChannelIndex;

inline EditBox add_digit_simple(
    doc::Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    PatternAndBeat time,
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
    return add_digit_simple(d, 0, 0, PatternAndBeat{0, 0}, sc::Volume{}, 0, 0x1);
}

EditBox volume_0_alt(doc::Document const& d) {
    return add_digit_simple(d, 0, 0, PatternAndBeat{0, 0}, sc::Volume{}, 0, 0x3);
}

EditBox volume_1(doc::Document const& d) {
    return add_digit_simple(d, 0, 0, PatternAndBeat{0, 0}, sc::Volume{}, 1, 0x2);
}

EditBox instr_0(doc::Document const& d) {
    return add_digit_simple(d, 0, 0, PatternAndBeat{0, 0}, sc::Instrument{}, 0, 0x1);
}

EditBox instr_1(doc::Document const& d) {
    return add_digit_simple(d, 0, 0, PatternAndBeat{0, 0}, sc::Instrument{}, 1, 0x2);
}


TEST_CASE("Check that volume editing operations are coalesced") {
    test_pattern_edits(volume_0, volume_1, true);
}

TEST_CASE("Check that 'first digit' volume edits are not coalesced") {
    test_pattern_edits(volume_0, volume_0_alt, false);
}

// The GUI is intended to make it impossible to enter a "volume digit 1"
// except for right after "volume digit 0", without moving the cursor.
// So it's not worth unit-testing volume 1 and 0 located at different spots.

TEST_CASE("Check that mixing volume/instrument edits are not coalesced") {
    test_pattern_edits(volume_0, instr_0, false);
}

TEST_CASE("Check that instrument edits are coalesced") {
    test_pattern_edits(instr_0, instr_1, true);
}

TEST_CASE("Check that 'first digit' instrument edits are coalesced") {
    test_pattern_edits(instr_0, instr_1, true);
}

}
