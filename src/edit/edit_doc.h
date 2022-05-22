#pragma once

#include "edit_common.h"
#include "doc.h"

namespace edit::edit_doc {

[[nodiscard]] EditBox set_tempo(double tempo);

[[nodiscard]] EditBox set_beats_per_measure(int measure_len);

[[nodiscard]] EditBox set_sequencer_options(
    doc::Document const& document, doc::SequencerOptions options
);

// # Track operations.

// TODO implement block editing (here or in edit_pattern.h?)

}
