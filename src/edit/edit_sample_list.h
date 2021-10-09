#pragma once

#include "edit_common.h"
#include <tuple>

namespace edit::edit_sample_list {

using doc::Document;
using doc::SampleIndex;

// Adding/removing samples.

/// Searches for an empty slot starting at `begin_idx` (which may be zero),
/// and adds the sample to the first empty slot found.
/// Returns {command, new sample index}.
/// If all slots starting at `begin_idx` are full, returns {nullptr, 0}.
[[nodiscard]]
std::tuple<MaybeEditBox, SampleIndex> try_add_sample(
    Document const& doc, SampleIndex begin_idx, doc::Sample sample
);

/// Adds the sample to the slot, replacing the existing sample if present.
[[nodiscard]]
EditBox replace_sample(Document const& doc, SampleIndex idx, doc::Sample sample);

/// Searches for an empty slot starting at `begin_idx` (which may be zero),
/// and clones sample `old_idx` into the first empty slot found.
/// Returns {command, new sample index}.
/// If `old_idx` has no sample or all slots starting at `begin_idx` are full,
/// returns {nullptr, 0}.
[[nodiscard]]
std::tuple<MaybeEditBox, SampleIndex> try_clone_sample(
    Document const& doc, SampleIndex old_idx, SampleIndex begin_idx
);

/// Tries to remove an sample at the specified slot and move the cursor to a
/// new non-empty slot (leaving it unchanged if no samples are left).
/// Returns {command, new sample index}.
/// If the slot has no sample, returns {nullptr, 0}.
[[nodiscard]]
std::tuple<MaybeEditBox, SampleIndex> try_remove_sample(
    Document const& doc, SampleIndex sample_idx
);

/// Tries to rename an sample.
/// If the slot has no sample, returns nullptr.
[[nodiscard]] MaybeEditBox try_rename_sample(
    Document const& doc, SampleIndex sample_idx, std::string new_name
);

// Reordering samples.

/// Returns a command which swaps two samples in the sample list,
/// and iterates over every pattern in the timeline to swap samples.
///
/// When clone_for_audio() is called, precomputes a copy of the current timeline
/// with the samples swapped (takes extra RAM, but is O(1) to apply on the
/// audio thread no matter how many patterns were edited).
[[nodiscard]] EditBox swap_samples(SampleIndex a, SampleIndex b);

}
