#pragma once

#include "modified_common.h"

namespace edit::modified {

// TODO flesh out sample editing.
// Perhaps instead of ModifiedFlags, we can return a unique_ptr<ModifiedMetadata>
// consisting of bitflags, as well as std::optional/variant for sample rearranging
// (which sample changed, add/remove metadata, etc.) allowing the driver to compute
// the old and new address of each sample.
// Or perhaps just add a method returning `AnonymousVariant &`
// (only by including a specific header), or allow downcasting, or idk even...

enum ModifiedFlags : ModifiedInt {
    /// The timeline of grid cells has been changed. The playback point may be invalid.
    TimelineRows = 0x1,
    /// Events within some patterns have changed.
    Patterns = 0x2,
    /// The song's "ticks per beat" value has changed.
    TicksPerBeat = 0x4,

    /// Sample data has changed, but the memory layout (order and size of samples) has not.
    /// Keep playing existing notes.
    SamplesEdited = 0x8,
    /// Repack all samples into RAM, and stop playing notes.
    SamplesMoved = 0x10,
};

}
