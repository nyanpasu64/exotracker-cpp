#pragma once

#include "../synth_common.h"
#include <memory>

namespace audio {
namespace synth {
namespace nes_2a03 {

// The actual NesApu1Synth and NesApu2Synth are not exposed.
// This way, changing the class definitions doesn't recompile the entire program.

class BaseNesApu1Synth : public BaseNesSynth {};
std::unique_ptr<BaseNesApu1Synth> make_NesApu1Synth(Blip_Buffer & blip);

std::unique_ptr<BaseNesSynth> make_NesApu2Synth(
    Blip_Buffer & blip, BaseNesApu1Synth & apu1
);

// End namespaces
}
}
}

