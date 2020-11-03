#pragma once

#include "chip_common.h"

#include <cstdint>

namespace chip_kinds {

#define FOREACH_CHIP_KIND(X) \
    X(Apu1) \
    X(Nes)

/// List of sound chips supported.
enum class ChipKind : uint32_t {
    #define DEF_ENUM_MEMBER(NAME)  NAME,
    FOREACH_CHIP_KIND(DEF_ENUM_MEMBER)
    #undef DEF_ENUM_MEMBER

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
