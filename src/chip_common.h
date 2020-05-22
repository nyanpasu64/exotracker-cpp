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

}

namespace chip_kinds {

enum class ChipKind : uint32_t;

}
