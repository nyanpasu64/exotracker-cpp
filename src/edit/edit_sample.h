#pragma once

#include "edit_common.h"
#include "doc.h"

namespace edit::edit_sample {

using doc::Chromatic;

// Sample edits. All replace the entire sample,
// and merge with other edits of the same sample index.

[[nodiscard]]
EditBox set_loop_byte(doc::Document const& doc, size_t sample_idx, uint16_t loop_byte);

[[nodiscard]]
EditBox set_sample_rate(
    doc::Document const& doc, size_t sample_idx, uint32_t sample_rate
);

[[nodiscard]]
EditBox set_root_key(doc::Document const& doc, size_t sample_idx, Chromatic root_key);

[[nodiscard]]
EditBox set_detune_cents(
    doc::Document const& doc, size_t sample_idx, int16_t detune_cents
);

}
