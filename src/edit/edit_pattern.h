#pragma once

#include "edit_common.h"
#include "doc.h"
#include "chip_common.h"
#include "timing_common.h"
#include "gui/config/block_config.h"

#include <cstdint>
#include <memory>
#include <tuple>
#include <variant>

namespace edit::edit_pattern {

namespace SubColumn_ {
    struct Note {
        DEFAULT_EQUALABLE(Note)
    };
    struct Instrument {
        DEFAULT_EQUALABLE(Instrument)
    };
    struct Volume {
        DEFAULT_EQUALABLE(Volume)
    };
    struct Effect {
        uint8_t effect_col;
        DEFAULT_EQUALABLE(Effect)
    };

    using SubColumn = std::variant<Note, Instrument, Volume, Effect>;
}

using SubColumn_::SubColumn;

using doc::Document;
using timing::TickT;
using chip_common::ChipIndex;
using chip_common::ChannelIndex;
using gui::config::block_config::ExtendBlock;

// You can't pass Cursor into edit functions,
// because Cursor stores (column: int, subcolumn: int)
// but edit functions need (chip: int, channel: int, subcolumn: SubColumn).

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
    TickT now
);

/// Insert note at current beat fraction, reusing last existing event if it exists.
/// If note is cut, erases instrument (not volume) and ignores argument.
/// If instrument is passed in, overwrites instrument. Otherwise leaves existing value.
/// TODO If volume is passed in, overwrites volume. Otherwise leaves existing value.
[[nodiscard]] EditBox insert_note(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    TickT now,
    ExtendBlock block_mode,
    doc::Note note,
    std::optional<doc::InstrumentIndex> instrument);

using MultiDigitField = std::variant<
    SubColumn_::Instrument, SubColumn_::Volume, SubColumn_::Effect
>;

/// How to edit a byte field when the user enters a nybble.
/// This enum will be changed once each nybble is editable independently.
enum class DigitAction {
    /// Given nybble x, replace byte ab with 0x.
    Replace,

    /// Given nybble x, replace byte ab with bx.
    /// Currently unused until I figure out how to encode two-digit hex values
    /// where the cursor occupies both digits.
    ShiftLeft,

    /// Given nybble x, replace byte ab with xb.
    UpperNybble,

    /// Given nybble x, replace byte ab with ax.
    LowerNybble,
};

/// Called when the user enters hex digits into a 1 or 2 digit hex field.
/// digit_action determines how the existing byte is incorporated.
///
/// Returns:
/// - uint8_t = new value of hex field.
/// - EditBox describes the edit to the document.
[[nodiscard]] std::tuple<uint8_t, EditBox> add_digit(
    Document const & document,
    ChipIndex chip,
    ChannelIndex channel,
    TickT now,
    ExtendBlock block_mode,
    MultiDigitField subcolumn,
    DigitAction digit_action,
    uint8_t nybble);


namespace EffectAction_ {
    /// Replace the effect name with the name passed in.
    struct Replace {
        doc::EffectName name;

        Replace(char const* name) : name{name[0], name[1]} {}
    };

    /// Replace the left character of the effect name.
    /// If there was no effect before, set the right character to '0'
    /// (doc::EFFECT_NAME_PLACEHOLDER).
    struct LeftChar {
        char c;
    };

    /// Replace the right character of the effect name.
    /// If there was no effect before, set the left character to '0'
    /// (doc::EFFECT_NAME_PLACEHOLDER).
    struct RightChar {
        char c;
    };

    using EffectAction = std::variant<Replace, LeftChar, RightChar>;
}

using EffectAction_::EffectAction;

[[nodiscard]] EditBox add_effect_char(
    Document const& document,
    ChipIndex chip,
    ChannelIndex channel,
    TickT now,
    ExtendBlock block_mode,
    SubColumn_::Effect subcolumn,
    EffectAction effect_action);

}
