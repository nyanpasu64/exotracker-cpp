#pragma once

#include "chip_common.h"

#include <cstdint>

namespace chip_kinds {

/// List of sound chips supported.
enum class ChipKind : uint32_t {
    Apu1,
    Nes,

    COUNT,
};

enum class Apu1ChannelID {
    Pulse1,
    Pulse2,
    COUNT,
};

/// Full APU 1+2
enum class NesChannelID {
    Pulse1,
    Pulse2,
    Tri,
    Noise,
    Dpcm,
    COUNT,
};

}
