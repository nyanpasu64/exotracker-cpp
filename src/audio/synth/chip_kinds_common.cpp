#include "chip_kinds_common.h"

namespace audio::synth::chip_kinds {

const ChipToNchan CHIP_TO_NCHAN = []() {
    /// EnumMap<ChipKind, ChannelIndex>
    ChipToNchan chip_to_nchan;
    chip_to_nchan.fill(0);

#define INITIALIZE(chip)  chip_to_nchan[ChipKind::chip] = enum_count<chip ## ChannelID>;
    INITIALIZE(Apu1)

    for (ChannelIndex nchan : chip_to_nchan) {
        if (nchan == 0) {
            throw std::logic_error(
                "Code error: ChipKind without an initialized channel count!"
            );
        }
    }

    return chip_to_nchan;
}();

}
