#pragma once

// audio/*.h depends on this file.
// To avoid circular include, this file should NOT include audio/*.h.
// See /src/DESIGN.md for naming and inclusion rules to follow.

#include <cstdint>

#include "event_queue.h"

namespace audio {

using Amplitude = int16_t;
using event_queue::ClockT;

struct AudioOptions {
    // Passed to synth only.
    ClockT clocks_per_sound_update;
};

}
