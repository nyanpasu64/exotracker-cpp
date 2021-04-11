#pragma once

#include "edit_common.h"
#include "doc.h"

namespace edit::edit_doc {

[[nodiscard]] EditBox set_tempo(int tempo);

// # Timeline operations.

[[nodiscard]] EditBox add_timeline_row(
    doc::Document const& document, doc::GridIndex grid_pos, doc::BeatFraction nbeats
);

[[nodiscard]] EditBox remove_timeline_row(doc::GridIndex grid_pos);

[[nodiscard]]
EditBox set_grid_length(doc::GridIndex grid_pos, doc::BeatFraction nbeats);

[[nodiscard]] EditBox move_grid_up(doc::GridIndex grid_pos);

[[nodiscard]] EditBox move_grid_down(doc::GridIndex grid_pos);

[[nodiscard]]
EditBox clone_timeline_row(doc::Document const& document, doc::GridIndex grid_pos);

}
