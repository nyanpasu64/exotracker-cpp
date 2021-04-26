#pragma once

#include <cstdint>
#include "util/enum_map.h"

namespace chip_common {

/// Index into a list of active sound chips.
using ChipIndex = uint32_t;
using ChannelIndex = uint32_t;

/// The maximum number of chips a module can contain.
/// Documents with more sound chips are rejected.
ChipIndex constexpr MAX_NCHIP = 255;

using ChipToNchan = ChannelIndex const*;

/// [ChipKind] ChannelIndex, but ChipKind is not declared yet.
///
/// WARNING: Some (future) chips (like N163) have a configurable number of channels.
/// DocumentCopy::chip_index_to_nchan() will handle this case,
/// while this array will return incorrect results (like 8 channels).
extern const ChipToNchan CHIP_TO_NCHAN;

/// The maximum number of channels that any future chip will ever contain.
/// Documents with unrecognized sound chips holding more channels are rejected.
constexpr size_t MAX_NCHAN_PER_CHIP = 255;

}

namespace chip_kinds {

enum class ChipKind : uint32_t;

}
