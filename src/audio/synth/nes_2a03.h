#pragma once

#include "../synth_common.h"
#include <memory>

namespace audio {
namespace synth {
namespace nes_2a03 {

std::unique_ptr<NesChipSynth> make_Nes2A03Synth(Blip_Buffer & blip);

// End namespaces
}
}
}

