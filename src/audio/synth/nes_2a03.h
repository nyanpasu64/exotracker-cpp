#pragma once

#include "../synth_common.h"
#include <memory>

namespace audio {
namespace synth {
namespace nes_2a03 {

// The actual NesApu1Synth and NesApu2Synth are not exposed.
// This way, changing the class definitions doesn't recompile the entire program.

class BaseApu1Instance : public ChipInstance {};
std::unique_ptr<BaseApu1Instance> make_Apu1Instance(
    chip_common::ChipIndex chip_index,
    ClockT clocks_per_sec,
    doc::FrequenciesRef frequencies,
    ClockT clocks_per_sound_update
);

//std::unique_ptr<ChipInstance> make_Apu2Instance(
//    Blip_Buffer & blip, BaseApu1Instance & apu1
//);

// End namespaces
}
}
}

