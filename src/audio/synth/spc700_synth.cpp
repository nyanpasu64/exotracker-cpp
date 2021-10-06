#include "spc700_synth.h"
#include "spc700_driver.h"
#include "impl_chip_common.h"

#include <algorithm>  // std::fill
#include <memory>

namespace audio::synth::spc700_synth {

Spc700Inner::Spc700Inner() {
    chip.init(ram_64k);
}

void Spc700Inner::reset() {
    using std::begin, std::end;
    std::fill(begin(ram_64k), end(ram_64k), 0);

    chip = SPC_DSP();
    chip.init(ram_64k);
}

Spc700Synth::Spc700Synth()
    : _p(std::make_unique<Spc700Inner>())
{}

void Spc700Synth::reset() {
    _p->reset();
}

void Spc700Synth::write_reg(RegisterWrite write) {
    _p->chip.write(write.address, write.value);
}

NsampWritten Spc700Synth::run_clocks(
    ClockT const nclk, WriteTo write_to
) {
    _p->chip.set_output(write_to.data(), write_to.size());
    _p->chip.run((int) nclk);
    return NsampT(_p->chip.out_pos() - write_to.data()) / STEREO_NCHAN;
}

}
