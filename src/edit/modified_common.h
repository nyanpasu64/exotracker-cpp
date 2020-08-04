#pragma once

#include <cstdint>

namespace edit::modified {

using ModifiedInt = uint32_t;

// Using a forward-declared enum lets us add new modification flags
// while only recompiling edit factories and OverallSynth.
// If each flag was a method, adding new methods would trigger recompiles.
enum ModifiedFlags : ModifiedInt;

}
