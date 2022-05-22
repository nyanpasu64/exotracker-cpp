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
    /// Song length, blocks or patterns, or events have changed. The playback point may
    /// now point out of bounds.
    Patterns = 0x1,

    /// SequencerOptions::target_tempo or ticks_per_beat or spc_timer_period has
    /// changed.
    EngineTempo = 0x10,

    /// Any field in SequencerOptions has changed.
    AllSequencerOptions = EngineTempo,

    /// Sample metadata has changed, but the actual data has not.
    /// Keep playing existing notes. (TODO reload tuning/loop points)
    SampleMetadataEdited = 0x100,
    /// Sample data and/or sizes have changed.
    /// Repack all samples into RAM, and stop currently-playing notes.
    /// If set, SampleMetadataEdited is ignored.
    SamplesEdited = 0x200,

    /// Instruments edited. Nothing checks for this so far,
    /// but include it for completeness.
    InstrumentsEdited = 0x1000,
};

}
