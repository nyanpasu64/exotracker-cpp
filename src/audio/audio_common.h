#pragma once

// audio/*.h depends on this file. To avoid circular include, audio.h should NOT include audio/*.h.
// See /src/DESIGN.md for naming and inclusion rules to follow.

#include <cstdint>

#include "event_queue.h"

namespace audio {

using Amplitude = int16_t;
using event_queue::ClockT;

struct AudioOptions {
    ClockT clocks_per_sound_update;
};

}
