#include "chip_kinds.h"
#include "chip_common.h"

#include <stdexcept>  // std::logic_error

namespace chip_common {

using namespace chip_kinds;


// # CHIP_TO_NCHAN

using ChipToNchanSized = EnumMap<ChipKind, ChannelIndex>;

static constinit ChipToNchanSized CHIP_TO_NCHAN_SIZED = [] {
    /// EnumMap<ChipKind, ChannelIndex>
    ChipToNchanSized chip_to_nchan{};

#define INITIALIZE(chip)  chip_to_nchan[ChipKind::chip] = enum_count<chip##ChannelID>;
    INITIALIZE(Apu1)
    INITIALIZE(Nes)
#undef INITIALIZE

    for (ChannelIndex nchan : chip_to_nchan) {
        if (nchan == 0) {
            throw std::logic_error(
                "Code error: ChipKind without an initialized channel count!"
            );
        }
    }

    return chip_to_nchan;
}();

constinit const ChipToNchan CHIP_TO_NCHAN = &CHIP_TO_NCHAN_SIZED[0];


// # CHIP_CHANNEL_TO_VOLUME_DIGITS

template<typename EnumT, typename ValueT>
using EnumArray = std::array<ValueT, enum_count<EnumT>>;

/// APU1 pulse 1/2 has 4-bit volumes, or 1 digit.
/// FIXME I changed it to 2 digits as a test. Remove this before pushing.
static const EnumArray<Apu1ChannelID, uint8_t> Apu1_VOL_DIGITS{1, 2};
static const EnumArray<NesChannelID, uint8_t> Nes_VOL_DIGITS{1, 1, 1, 1, 2};

using ChipChannelToVolumeDigitsSized = EnumMap<ChipKind, uint8_t const*>;

static constinit ChipChannelToVolumeDigitsSized CHIP_CHANNEL_TO_VOLUME_DIGITS_SIZED = []() {
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

}
