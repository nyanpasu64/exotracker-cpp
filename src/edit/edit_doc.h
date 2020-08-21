#pragma once

#include "edit_common.h"
#include "doc.h"

namespace edit::edit_doc {

[[nodiscard]] EditBox set_ticks_per_beat(int ticks_per_beat);

// # Timeline operations.

[[nodiscard]] EditBox add_timeline_row(
    doc::Document const& document, doc::GridIndex grid_pos, doc::BeatFraction nbeats
);

[[nodiscard]]
EditBox remove_timeline_row(doc::Document const& document, doc::GridIndex grid_pos);

}
