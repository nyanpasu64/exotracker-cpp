#include "spc700_p.h"
#include "spc700_driver.h"
#include "impl_chip_common.h"

#include <gsl/span>

#include <memory>

namespace audio::synth::spc700 {

using spc700_driver::Spc700Driver;
using impl_chip::ImplChipInstance;


using Spc700Instance = ImplChipInstance<Spc700Driver, Spc700Synth>;

Spc700Synth::Spc700Synth() {
    _chip.init(_ram_64k);
}

void Spc700Synth::write_memory(RegisterWrite write) {
    _ram_64k[write.address] = write.value;
    _chip.write(write.address, write.value);
}

ChipInstance::NsampWritten Spc700Synth::run_clocks(
    ClockT const nclk, WriteTo write_to
) {
    _chip.set_output(write_to.data(), write_to.size());
    _chip.run(nclk);
    return NsampT(_chip.out_pos() - write_to.data()) / STEREO_NCHAN;
}

// # Public interface

std::unique_ptr<ChipInstance> make_Spc700Instance(
    chip_common::ChipIndex chip_index,
    NsampT samples_per_sec,
    doc::FrequenciesRef frequencies)
{
    return std::make_unique<Spc700Instance>(
        chip_index,
        Spc700Driver(samples_per_sec, frequencies),
        Spc700Synth());
}

}
