#pragma once

#include "../synth_common.h"
#include <memory>

namespace audio {
namespace synth {
namespace nes_2a03 {

// The actual ChipInstance subclasses are not exposed.
// This way, changing the class definitions doesn't recompile the entire program.

std::unique_ptr<ChipInstance> make_Apu1Instance(
    chip_common::ChipIndex chip_index,
    ClockT clocks_per_sec,
    doc::FrequenciesRef frequencies,
    ClockT clocks_per_sound_update);

std::unique_ptr<ChipInstance> make_NesInstance(
    chip_common::ChipIndex chip_index,
    ClockT clocks_per_sec,
    doc::FrequenciesRef frequencies,
    ClockT clocks_per_sound_update);

// End namespaces
}
}
}

