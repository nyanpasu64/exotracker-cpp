#pragma once

#include "edit_common.h"
#include "doc.h"
#include "chip_common.h"
#include "timing_common.h"

#include <cstdint>
#include <memory>
#include <tuple>
#include <variant>

namespace edit::edit_pattern {

namespace subcolumns {
    struct Note {
        DEFAULT_EQUALABLE(Note)
    };
    struct Instrument {
        DEFAULT_EQUALABLE(Instrument)
    };
    struct Volume {
        DEFAULT_EQUALABLE(Volume)
    };
    struct EffectName {
        uint8_t effect_col;

        DEFAULT_EQUALABLE(EffectName)
    };
    struct EffectValue {
        uint8_t effect_col;

        DEFAULT_EQUALABLE(EffectValue)
    };

    using SubColumn = std::variant<
        Note, Instrument, Volume, EffectName, EffectValue
    >;
}

using subcolumns::SubColumn;

using doc::Document;
using timing::GridAndBeat;
using chip_common::ChipIndex;
using chip_common::ChannelIndex;

// You can't pass Cursor into edit functions,
// because Cursor stores (column: int, subcolumn: int)
// but edit functions need (chip: int, channel: int, subcolumn: SubColumn).

/// Calling this in space not occupied by a block creates a block.
/// Calling this in space not occupied by a block returns a no-op edit.
///
/// Currently only used by unit tests. Could be exposed to users through the GUI,
/// but the functionality can be achieved by inserting a note
/// (or maybe even deleting a non-existent event).
[[nodiscard]] EditBox create_block(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    GridAndBeat abs_time
);

/// Clear the focused subcolumn of all events
/// anchored exactly to the current beat fraction.
/// Deleting the note column also clears instrument and volume.
///
/// Calling this in space not occupied by a block returns a no-op edit.
[[nodiscard]] EditBox delete_cell(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    SubColumn subcolumn,
    GridAndBeat abs_time
);

/// Insert note at current beat fraction, reusing last existing event if it exists.
/// If note is cut, erases instrument (not volume) and ignores argument.
/// If instrument is passed in, overwrites instrument. Otherwise leaves existing value.
/// TODO If volume is passed in, overwrites volume. Otherwise leaves existing value.
[[nodiscard]] EditBox insert_note(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    GridAndBeat abs_time,
    doc::Note note,
    std::optional<doc::InstrumentIndex> instrument
);

using MultiDigitField = std::variant<subcolumns::Instrument, subcolumns::Volume>;

/// How to edit a byte field when the user enters a nybble.
/// This enum will be changed once each nybble is editable independently.
enum class DigitAction {
    /// Given nybble a, replace byte xy with 0a.
    Replace,

    /// The byte is assumed to have upper nybble 0.
    /// Given nybble b, replace byte 0a with ab.
    ShiftLeft,
};

/// Called when the user enters notes into a two-digit hex field.
///
/// On first keypress, digit_index is 0, and this function erases the field
/// and replaces it with the nybble entered by the user.
///
/// If the user presses another key immediately afterwards (without moving the cursor),
/// add_digit() is called with digit_index = 1.
/// It moves the existing nybble to the left, and adds the nybble entered by the user.
/// When the second keypress is sent to History, key0.can_coalesce(key1) returns true,
/// and the two inputs are merged in the undo history.
[[nodiscard]] std::tuple<uint8_t, EditBox> add_digit(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    GridAndBeat abs_time,
    MultiDigitField subcolumn,
    DigitAction digit_action,
    uint8_t nybble);

}
