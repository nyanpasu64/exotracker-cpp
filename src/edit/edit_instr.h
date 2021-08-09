#pragma once

#include "edit_common.h"
#include "doc.h"

#include <optional>
#include <tuple>

namespace edit::edit_instr {

// Keysplit edits which add, remove, or reorder patches.
// They do not coalesce with other operations.

/// Returns nullptr if adding a patch would exceed MAX_KEYSPLITS.
[[nodiscard]] MaybeEditBox try_add_patch(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx
);

/// Returns nullptr if removing last patch in keysplit, or keysplit is empty.
[[nodiscard]] MaybeEditBox try_remove_patch(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx
);

/// Returns nullptr if moving patch 0 up.
[[nodiscard]] MaybeEditBox try_move_patch_up(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx
);

/// Returns nullptr if moving patch >= N-1 down.
/// This includes trying to move patch 0 down in an empty keysplit.
[[nodiscard]] MaybeEditBox try_move_patch_down(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx
);

/// Sets the minimum key of the active patch,
/// and moves it into sorted order by minimum key relative to other patches.
/// Returns the edit and the new index of the active patch.
/// Coalesces with other set_min_key() edits.
[[nodiscard]] std::tuple<EditBox, size_t> set_min_key(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, doc::Chromatic value
);

// Single-patch edits. All replace the entire patch,
// and coalesce with other edits of the same instrument and patch index.

[[nodiscard]] EditBox set_sample_idx(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, doc::SampleIndex value
);

[[nodiscard]] EditBox set_attack(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, uint8_t value
);

[[nodiscard]] EditBox set_decay(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, uint8_t value
);

[[nodiscard]] EditBox set_sustain(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, uint8_t value
);

[[nodiscard]] EditBox set_decay2(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, uint8_t value
);

}  // namespace

