#include "spc700_synth.h"
#include "spc700_driver.h"
#include "impl_chip_common.h"

namespace audio::synth::spc700 {

using spc700_driver::Spc700Driver;
using spc700_synth::Spc700Synth;
using impl_chip::ImplChipInstance;

std::unique_ptr<ChipInstance> make_Spc700Instance(
    chip_common::ChipIndex chip_index,
    NsampT samples_per_sec,
    doc::FrequenciesRef frequencies)
{
    return std::make_unique<ImplChipInstance<Spc700Driver, Spc700Synth>>(
        chip_index,
        Spc700Driver(samples_per_sec, frequencies),
        Spc700Synth());
}

}
