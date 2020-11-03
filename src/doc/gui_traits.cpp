#include "gui_traits.h"
#include "chip_kinds.h"
#include "util/release_assert.h"

namespace doc::gui_traits {

using namespace chip_kinds;

/// Similar to EnumMap.
/// Pros: supports aggregate initialization.
/// Cons: cannot be indexed using EnumT.
template<typename EnumT, typename ValueT>
using EnumArray = std::array<ValueT, enum_count<EnumT>>;


// # CHIP_CHANNEL_TO_VOLUME_DIGITS, get_volume_digits()

/// APU1 pulse 1/2 has 4-bit volumes, or 1 digit.
static const EnumArray<Apu1ChannelID, uint8_t> Apu1_VOL_DIGITS{1, 1};

/// Triangle has no volume control, but use 1 digit as a mute control.
/// Noise has 4-bit volumes.
/// For DPCM, we map the volume column to the 7-bit current DPCM level.
static const EnumArray<NesChannelID, uint8_t> Nes_VOL_DIGITS{1, 1, 1, 1, 2};

static constinit const auto CHIP_CHANNEL_TO_VOLUME_DIGITS_SIZED = []() {
    // Compare to chip_common.cpp.
    EnumMap<ChipKind, uint8_t const*> out{};

    #define INITIALIZE(chip)  out[ChipKind::chip] = chip##_VOL_DIGITS.data();
    FOREACH_CHIP_KIND(INITIALIZE)
    #undef INITIALIZE

    return out;
}();

constinit const ChipChannelToVolumeDigits CHIP_CHANNEL_TO_VOLUME_DIGITS =
    CHIP_CHANNEL_TO_VOLUME_DIGITS_SIZED.data();

uint8_t get_volume_digits(Document const& doc, ChipIndex chip, ChannelIndex channel) {
    release_assert(chip < doc.chips.size());
    auto chip_kind = (size_t) doc.chips[chip];

    release_assert(chip_kind < (size_t) ChipKind::COUNT);
    release_assert(channel < chip_common::CHIP_TO_NCHAN[chip_kind]);
    return CHIP_CHANNEL_TO_VOLUME_DIGITS[chip_kind][channel];
}


// # is_noise()

bool is_noise(Document const& doc, ChipIndex chip, ChannelIndex channel) {
    release_assert(chip < doc.chips.size());
    if (doc.chips[chip] == chip_kinds::ChipKind::Nes) {
        return channel == (ChannelIndex) chip_kinds::NesChannelID::Noise;
    }

    return false;
}


// # channel_name()

static constinit const EnumArray<Apu1ChannelID, char const*> Apu1_CHANNEL_NAMES{
    "Pulse 1", "Pulse 2",
};
static constinit const EnumArray<NesChannelID, char const*> Nes_CHANNEL_NAMES{
    "Pulse 1", "Pulse 2", "Triangle", "Noise", "DPCM"
};

static constinit const auto CHIP_CHANNEL_NAME = [] () {
    EnumMap<ChipKind, char const* const*> out{};

    #define INITIALIZE(chip)  out[ChipKind::chip] = chip##_CHANNEL_NAMES.data();
    FOREACH_CHIP_KIND(INITIALIZE)
    #undef INITIALIZE

    return out;
}();

char const* channel_name(Document const& doc, ChipIndex chip, ChannelIndex channel) {
    release_assert(chip < doc.chips.size());
    auto chip_kind = (size_t) doc.chips[chip];

    release_assert(chip_kind < (size_t) ChipKind::COUNT);
    release_assert(channel < chip_common::CHIP_TO_NCHAN[chip_kind]);

    return CHIP_CHANNEL_NAME[chip_kind][channel];
}

}
