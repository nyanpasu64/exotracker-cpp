#include "chip_kinds.h"
#include "chip_common.h"
#include "util/constinit.h"

#include <stdexcept>  // std::logic_error

namespace chip_common {

using namespace chip_kinds;


// # CHIP_TO_NCHAN

static constinit const auto CHIP_TO_NCHAN_SIZED = [] {
    /// EnumMap<ChipKind, ChannelIndex>
    EnumMap<ChipKind, ChannelIndex> chip_to_nchan{};

    #define INITIALIZE(chip)  chip_to_nchan[ChipKind::chip] = enum_count<chip##ChannelID>;
    FOREACH_CHIP_KIND(INITIALIZE)
    #undef INITIALIZE

    return chip_to_nchan;
}();

constinit const ChipToNchan CHIP_TO_NCHAN = &CHIP_TO_NCHAN_SIZED[0];

}
