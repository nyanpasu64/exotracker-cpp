#include "gui_traits.h"
#include "chip_kinds.h"
#include "util/release_assert.h"

namespace doc::gui_traits {

using namespace chip_kinds;

template<typename EnumT, typename ValueT>
using EnumArray = std::array<ValueT, enum_count<EnumT>>;

/// APU1 pulse 1/2 has 4-bit volumes, or 1 digit.
/// FIXME I changed it to 2 digits as a test. Remove this before pushing.
static const EnumArray<Apu1ChannelID, uint8_t> Apu1_VOL_DIGITS{1, 2};
static const EnumArray<NesChannelID, uint8_t> Nes_VOL_DIGITS{1, 1, 1, 1, 2};

using ChipChannelToVolumeDigitsSized = EnumMap<ChipKind, uint8_t const*>;

static constinit const
ChipChannelToVolumeDigitsSized CHIP_CHANNEL_TO_VOLUME_DIGITS_SIZED = []() {
    // Compare to chip_common.cpp.
    ChipChannelToVolumeDigitsSized out{};

    #define INITIALIZE(chip)  out[ChipKind::chip] = chip##_VOL_DIGITS.data();
    INITIALIZE(Apu1)
    INITIALIZE(Nes)
    #undef INITIALIZE

    for (auto chan_ptr : out) {
        if (chan_ptr == nullptr) {
            throw std::logic_error(
                "Code error: ChipKind without a list of volume widths!"
            );
        }
    }

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

}
