#pragma once

#include "edit_common.h"
#include "doc.h"

#include <optional>

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

// Single-patch edits. All replace the entire patch,
// and coalesce with other edits of the same instrument and patch index.

[[nodiscard]] EditBox edit_min_key(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, doc::Chromatic value
);

[[nodiscard]] EditBox edit_sample_idx(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, doc::SampleIndex value
);

[[nodiscard]] EditBox edit_attack(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, uint8_t value
);

[[nodiscard]] EditBox edit_decay(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, uint8_t value
);

[[nodiscard]] EditBox edit_sustain(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, uint8_t value
);

[[nodiscard]] EditBox edit_decay2(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, uint8_t value
);

}  // namespace

