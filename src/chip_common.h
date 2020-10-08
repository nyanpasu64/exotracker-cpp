#pragma once

#include <cstdint>
#include "util/enum_map.h"

namespace chip_common {

/// Index into a list of active sound chips.
using ChipIndex = uint32_t;
using ChannelIndex = uint32_t;

ChipIndex constexpr MAX_NCHIP = 100;

// [ChipKind] ChannelIndex, but ChipKind is not declared yet.
using ChipToNchan = ChannelIndex const *;
extern const ChipToNchan CHIP_TO_NCHAN;

// [k: ChipKind] [CHIP_TO_NCHAN[k]] number of digits.
using ChipChannelToVolumeDigits = uint8_t const* const*;
extern const ChipChannelToVolumeDigits CHIP_CHANNEL_TO_VOLUME_DIGITS;

}

namespace chip_kinds {

enum class ChipKind : uint32_t;

}
