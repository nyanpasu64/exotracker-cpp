#pragma once

#include "chip_common.h"

#include <cstdint>

namespace chip_kinds {

/// List of sound chips supported.
enum class ChipKind : uint32_t {
    Apu1,

    COUNT,
};

enum class Apu1ChannelID {
    Pulse1,
    Pulse2,
    COUNT,
};

}
