#pragma once

#include <cstdint>
#include "util/enum_map.h"

namespace chip_common {

/// Index into a list of active sound chips.
using ChipIndex = uint32_t;
using ChannelIndex = uint32_t;

ChipIndex constexpr MAX_NCHIP = 100;

using ChipToNchan = ChannelIndex const*;

/// [ChipKind] ChannelIndex, but ChipKind is not declared yet.
///
/// WARNING: Some (future) chips (like N163) have a configurable number of channels.
/// DocumentCopy::chip_index_to_nchan() will handle this case,
/// while this array will return incorrect results (like 8 channels).
extern const ChipToNchan CHIP_TO_NCHAN;

}

namespace chip_kinds {

enum class ChipKind : uint32_t;

}
