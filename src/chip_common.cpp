#include "chip_kinds.h"
#include "chip_common.h"

namespace chip_common {

using namespace chip_kinds;

using ChipToNchanSized = EnumMap<ChipKind, ChannelIndex>;

const ChipToNchanSized CHIP_TO_NCHAN_SIZED = [] {
    /// EnumMap<ChipKind, ChannelIndex>
    ChipToNchanSized chip_to_nchan;
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

const ChipToNchan CHIP_TO_NCHAN = &CHIP_TO_NCHAN_SIZED[0];

}
