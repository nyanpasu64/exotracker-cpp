#pragma once

#include "edit_common.h"
#include <tuple>

namespace edit::edit_instr_list {

using doc::Document;
using doc::InstrumentIndex;

// Adding/removing instruments.

/// Tries to add an empty instrument in the first empty slot.
/// Returns {command, new instrument index}.
/// If all slots are full, returns {nullptr, 0}.
[[nodiscard]]
std::tuple<MaybeEditBox, InstrumentIndex> try_add_instrument(Document const& doc);

/// Loops through slots starting at the specified slot,
/// and adds an empty instrument in the first empty slot found.
/// Returns {command, new instrument index}.
/// If all subsequent slots are full, returns {nullptr, 0}.
[[nodiscard]]
std::tuple<MaybeEditBox, InstrumentIndex> try_insert_instrument(
    Document const& doc, InstrumentIndex instr_idx
);

/// Tries to remove an instrument at the specified slot
/// and select the next non-empty slot (or leaving it unchanged otherwise).
/// Returns {command, new instrument index}.
/// If the slot has no instrument, returns {nullptr, 0}.
[[nodiscard]]
std::tuple<MaybeEditBox, InstrumentIndex> try_remove_instrument(
    Document const& doc, InstrumentIndex instr_idx
);

/// Tries to clone an instrument into the first empty slot.
/// Returns {command, new instrument index}.
/// If the slot has no instrument or all slots are full, returns {nullptr, 0}.
[[nodiscard]]
std::tuple<MaybeEditBox, InstrumentIndex> try_clone_instrument(
    Document const& doc, InstrumentIndex instr_idx
);

// TODO try_clone_to()

/// Tries to rename an instrument.
/// If the slot has no instrument, returns nullptr.
[[nodiscard]] MaybeEditBox try_rename_instrument(
    Document const& doc, InstrumentIndex instr_idx, std::string new_name
);

// Reordering instruments.

/// Returns a command which swaps two instruments in the instrument list,
/// and iterates over every pattern in the timeline to swap instruments (slow).
[[nodiscard]] EditBox swap_instruments(InstrumentIndex a, InstrumentIndex b);

/// Returns a command which swaps two instruments in the instrument list,
/// and swaps the current timeline and one with the instruments swapped
/// (eats RAM).
[[nodiscard]] EditBox swap_instruments_cached(
    Document const& doc, InstrumentIndex a, InstrumentIndex b
);

}
