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
using timing::PatternAndBeat;
using doc::SeqEntryIndex;
using chip_common::ChipIndex;
using chip_common::ChannelIndex;

// You can't pass Cursor into edit functions,
// because Cursor stores (column: int, subcolumn: int)
// but edit functions need (chip: int, channel: int, subcolumn: SubColumn).

/// Clear the focused subcolumn of all events
/// anchored exactly to the current beat fraction.
/// Deleting the note column also clears instrument and volume.
EditBox delete_cell(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    SubColumn subcolumn,
    PatternAndBeat time
);

/// Insert note at current beat fraction, reusing last existing event if it exists.
/// If note is cut, erases instrument (not volume) and ignores argument.
/// If instrument is passed in, overwrites instrument. Otherwise leaves existing value.
/// TODO If volume is passed in, overwrites volume. Otherwise leaves existing value.
EditBox insert_note(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    PatternAndBeat time,
    doc::Note note,
    std::optional<doc::InstrumentIndex> instrument
);

using MultiDigitField = std::variant<subcolumns::Instrument, subcolumns::Volume>;

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
std::tuple<uint8_t, EditBox> add_digit(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    PatternAndBeat time,
    MultiDigitField subcolumn,
    int digit_index,
    uint8_t nybble
);

}
