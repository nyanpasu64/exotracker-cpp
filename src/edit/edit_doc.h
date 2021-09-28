#pragma once

#include "edit_common.h"
#include "doc.h"

namespace edit::edit_doc {

[[nodiscard]] EditBox set_tempo(double tempo);

[[nodiscard]] EditBox set_sequencer_options(
    doc::Document const& document, doc::SequencerOptions options
);

// # Timeline operations.

/// It's the responsibility of the caller to avoid exceeding MAX_TIMELINE_FRAMES.
/// This may change.
[[nodiscard]] EditBox add_timeline_frame(
    doc::Document const& document, doc::GridIndex grid_pos, doc::BeatFraction nbeats
);

/// It's the responsibility of the caller to avoid removing the last row.
/// This may change.
[[nodiscard]] EditBox remove_timeline_frame(doc::GridIndex grid_pos);

[[nodiscard]]
EditBox set_grid_length(doc::GridIndex grid_pos, doc::BeatFraction nbeats);

[[nodiscard]] EditBox move_grid_up(doc::GridIndex grid_pos);

[[nodiscard]] EditBox move_grid_down(doc::GridIndex grid_pos);

[[nodiscard]]
EditBox clone_timeline_frame(doc::Document const& document, doc::GridIndex grid_pos);

}
