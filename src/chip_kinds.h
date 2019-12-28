#pragma once

#include "util/enum_map.h"

#include <cstdint>

namespace chip_kinds {

/// Index into a list of active sound chips.
using ChipIndex = uint32_t;
using ChannelIndex = uint32_t;

ChipIndex constexpr MAX_NCHIP = 100;

/// List of sound chips supported.
enum class ChipKind {
    Apu1,
//    Apu2,

    COUNT,
};

enum class Apu1ChannelID {
    Pulse1,
    Pulse2,
    COUNT,
};

using ChipToNchan = EnumMap<ChipKind, ChannelIndex>;
extern const ChipToNchan CHIP_TO_NCHAN;

}
