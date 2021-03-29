#include "spc700.h"
#include "spc700_driver.h"
#include "impl_chip_common.h"
#include "../synth_common.h"

#include <snes9x-dsp/SPC_DSP.h>

#include <cstdint>
#include <memory>

namespace audio::synth::spc700 {

using spc700_driver::Spc700Driver;
using impl_chip::ImplChipInstance;

class Spc700Synth {
    uint8_t _ram_64k[0x10000] = {};
    SPC_DSP _chip;

// impl
public:
    Spc700Synth() {
        _chip.init(_ram_64k);
    }

    void write_memory(RegisterWrite write) {
        _ram_64k[write.address] = write.value;
        _chip.write(write.address, write.value);
    }

    ChipInstance::NsampWritten run_clocks(
        ClockT const nclk,
        WriteTo write_to)
    {
        _chip.set_output(write_to.data(), write_to.size());
        _chip.run(nclk);
        return NsampT(_chip.out_pos() - write_to.data()) / STEREO_NCHAN;
    }

};

using Spc700Instance = ImplChipInstance<Spc700Driver, Spc700Synth>;

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
