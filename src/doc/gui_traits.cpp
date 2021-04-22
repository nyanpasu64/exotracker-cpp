#include "gui_traits.h"
#include "chip_kinds.h"
#include "util/constinit.h"
#include "util/release_assert.h"

namespace doc::gui_traits {

using namespace chip_kinds;

/// Similar to EnumMap.
/// Pros: supports aggregate initialization.
/// Cons: cannot be indexed using EnumT.
template<typename EnumT, typename ValueT>
using EnumArray = std::array<ValueT, enum_count<EnumT>>;


// # CHIP_CHANNEL_TO_VOLUME_DIGITS, get_volume_digits()

static const EnumArray<Spc700ChannelID, uint8_t> Spc700_VOL_DIGITS{2, 2, 2, 2, 2, 2, 2, 2};

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
    return false;
}


// # channel_name()

static constinit const EnumArray<Spc700ChannelID, char const*> Spc700_CHANNEL_NAMES{
    "Channel 1",
    "Channel 2",
    "Channel 3",
    "Channel 4",
    "Channel 5",
    "Channel 6",
    "Channel 7",
    "Channel 8",
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
