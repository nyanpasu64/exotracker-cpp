#pragma once

#include "chip_instance_common.h"

#include <memory>

namespace audio::synth::spc700 {

using chip_instance::ChipInstance;

std::unique_ptr<ChipInstance> make_Spc700Instance(
    chip_common::ChipIndex chip_index, doc::FrequenciesRef frequencies
);

}
