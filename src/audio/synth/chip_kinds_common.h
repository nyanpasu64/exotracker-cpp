#pragma once

#include "util/enum_map.h"

#include <cstdint>

namespace audio::synth::chip_kinds {

/// Index into a list of active sound chips.
using ChipIndex = uint32_t;
using ChannelIndex = uint32_t;

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

}
