#include "chip_kinds.h"
#include "chip_common.h"

#include <stdexcept>  // std::logic_error

namespace chip_common {

using namespace chip_kinds;


// # CHIP_TO_NCHAN

using ChipToNchanSized = EnumMap<ChipKind, ChannelIndex>;

static constinit const ChipToNchanSized CHIP_TO_NCHAN_SIZED = [] {
    /// EnumMap<ChipKind, ChannelIndex>
    ChipToNchanSized chip_to_nchan{};

    #define INITIALIZE(chip)  chip_to_nchan[ChipKind::chip] = enum_count<chip##ChannelID>;
    INITIALIZE(Apu1)
    INITIALIZE(Nes)
    #undef INITIALIZE

    for (ChannelIndex nchan : chip_to_nchan) {
        if (nchan == 0) {
            throw std::logic_error(
                "Code error: ChipKind without an initialized channel count!"
            );
        }
    }

    return chip_to_nchan;
}();

constinit const ChipToNchan CHIP_TO_NCHAN = &CHIP_TO_NCHAN_SIZED[0];

}
