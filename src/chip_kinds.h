#pragma once

#include "chip_common.h"

#include <cstdint>

namespace chip_kinds {

#define FOREACH_CHIP_KIND(X) \
    X(Spc700) \

/// List of sound chips supported.
enum class ChipKind : uint32_t {
    #define DEF_ENUM_MEMBER(NAME)  NAME,
    FOREACH_CHIP_KIND(DEF_ENUM_MEMBER)
    #undef DEF_ENUM_MEMBER

    COUNT,
};

enum class Spc700ChannelID {
    Channel1,
    Channel2,
    Channel3,
    Channel4,
    Channel5,
    Channel6,
    Channel7,
    Channel8,
    COUNT,
};

}
