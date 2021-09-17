#pragma once

#include "edit_common.h"
#include <tuple>

namespace edit::edit_instr_list {

using doc::Document;
using doc::InstrumentIndex;

// Adding/removing instruments.

/// Searches for an empty slot starting at `begin_idx` (which may be zero),
/// and adds an empty instrument in the first empty slot found.
/// Returns {command, new instrument index}.
/// If all slots starting at `begin_idx` are full, returns {nullptr, 0}.
[[nodiscard]]
std::tuple<MaybeEditBox, InstrumentIndex> try_add_instrument(
    Document const& doc, InstrumentIndex begin_idx
);

/// Searches for an empty slot starting at `begin_idx` (which may be zero),
/// and clones instrument `old_idx` into the first empty slot found.
/// Returns {command, new instrument index}.
/// If `old_idx` has no instrument or all slots starting at `begin_idx` are full,
/// returns {nullptr, 0}.
[[nodiscard]]
std::tuple<MaybeEditBox, InstrumentIndex> try_clone_instrument(
    Document const& doc, InstrumentIndex old_idx, InstrumentIndex begin_idx
);

/// Tries to remove an instrument at the specified slot and move the cursor to a
/// new non-empty slot (leaving it unchanged if no instruments are left).
/// Returns {command, new instrument index}.
/// If the slot has no instrument, returns {nullptr, 0}.
[[nodiscard]]
std::tuple<MaybeEditBox, InstrumentIndex> try_remove_instrument(
    Document const& doc, InstrumentIndex instr_idx
);

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
