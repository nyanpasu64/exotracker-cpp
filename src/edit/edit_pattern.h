#pragma once

#include "edit_common.h"
#include "doc.h"
#include "chip_common.h"
#include "timing_common.h"

#include <memory>
#include <tuple>
#include <variant>

namespace edit::edit_pattern {

namespace subcolumns {
    struct Note {
    };
    struct Instrument {
    };
    struct Volume {
    };
    struct EffectName {
        uint8_t effect_col;
    };
    struct EffectValue {
        uint8_t effect_col;
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

/// Erases an instrument field and replaces it with the nybble entered by the user.
EditBox instrument_digit_1(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    PatternAndBeat time,
    uint8_t nybble
);

/// Takes an instrument field with one nybble filled in,
/// moves the nybble to the left, and appends the nybble entered by the user.
/// Will be merged with previous instrument_digit_1() in undo history.
std::tuple<doc::InstrumentIndex, EditBox> instrument_digit_2(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    PatternAndBeat time,
    uint8_t nybble
);

}
