/// This file contains data shared between synth and sound_driver,
/// but hidden from individual sound chips (like synth/nes_2a03)
/// to reduce recompilation.
///
/// This data includes an enum of chips, an enum of channels,
/// and "vector of register writes" sent to each chip, etc.

#pragma once

#include "synth_common.h"
#include "util/enum_map.h"

#include <vector>


namespace audio {
namespace chips {

/// List of sound chips supported.
enum class NesChipID {
    NesApu1,
    NesApu2,

    COUNT,
    NotNesChip,
};

/// List of sound channels, belonging to chips.
enum class ChannelID {
    // NesApu1
    Pulse1,
    Pulse2,

    // NesApu2
    Tri,
    Noise,
    Dpcm,

    COUNT,
};

// From synth_common.h, not synth.h.
using synth::RegisterWrite;

using RegisterWrites = std::vector<RegisterWrite>;
using ChannelRegisterWrites = EnumMap<ChannelID, RegisterWrites>;

}
}
