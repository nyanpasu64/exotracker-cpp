#pragma once

#include "edit_common.h"

namespace edit::edit_instr_list {

using doc::Document;
using doc::InstrumentIndex;

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
