#pragma once

#include "doc/events.h"

#include <cstdint>

namespace audio::synth::volume_calc {

using doc::events::Volume;

/// Multiply two 4-bit volume values, returning a 4-bit value.
/// Copies FamiTracker's algorithm.
int volume_mul_4x4_4(int a, int b);

}
