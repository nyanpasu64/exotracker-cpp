#pragma once

#include "edit_common.h"
#include "doc.h"
#include "chip_common.h"
#include "timing_common.h"

#include <memory>
#include <variant>

namespace edit::pattern {

namespace subcolumns {
    struct Note {
        COMPARABLE(Note, ())
    };
    struct Instrument {
        COMPARABLE(Instrument, ())
    };
    struct Volume {
        COMPARABLE(Volume, ())
    };
    struct EffectName {
        uint8_t effect_col;
        COMPARABLE(EffectName, (effect_col))
    };
    struct EffectValue {
        uint8_t effect_col;
        COMPARABLE(EffectValue, (effect_col))
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
EditBox insert_note(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    PatternAndBeat time,
    doc::Note note
);

}
