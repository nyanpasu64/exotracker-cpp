#pragma once

#include "../synth_common.h"
#include "sequencer.h"
#include "util/macros.h"

namespace audio::synth::music_driver {

/// Unused at the moment. Possibly related to RegisterWriteQueue?
struct TimeRef {
    DISABLE_COPY_MOVE(TimeRef)
    synth::ClockT time;
};

/// An integer which should only take on values within a specific range.
/// This is purely for documentation. No compile-time or runtime checking is performed.
template<int begin, int end, typename T>
using Range = T;

// end namespace
}
