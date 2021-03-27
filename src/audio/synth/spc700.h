#pragma once

#include "audio/synth_common.h"

namespace audio::synth::spc700 {

std::unique_ptr<ChipInstance> make_Spc700Instance(
    chip_common::ChipIndex chip_index,
    ClockT clocks_per_sec,
    doc::FrequenciesRef frequencies);

}
