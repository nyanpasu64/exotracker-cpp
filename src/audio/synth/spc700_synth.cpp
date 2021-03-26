#include "spc700_synth.h"
#include "spc700_driver.h"
#include "impl_chip_common.h"

#include <memory>

namespace audio::synth::spc700_synth {

Spc700Synth::Spc700Synth() {
    _chip.init(_ram_64k);
}

void Spc700Synth::write_reg(RegisterWrite write) {
    _chip.write(write.address, write.value);
}

NsampWritten Spc700Synth::run_clocks(
    ClockT const nclk, WriteTo write_to
) {
    _chip.set_output(write_to.data(), write_to.size());
    _chip.run(nclk);
    return NsampT(_chip.out_pos() - write_to.data()) / STEREO_NCHAN;
}

}
