#include "synth_common.h"

namespace audio {
namespace synth {

const ChannelToNesChip CHANNEL_TO_NES_CHIP = []() {
    /// [ChannelID] NesChipID
    ChannelToNesChip channel_to_nes_chip;
    channel_to_nes_chip.fill(NesChipID::COUNT);

    channel_to_nes_chip[ChannelID::Pulse1] = NesChipID::NesApu1;
    channel_to_nes_chip[ChannelID::Pulse2] = NesChipID::NesApu1;

//    channel_to_nes_chip[ChannelID::Tri] = NesChipID::NesApu2;
//    channel_to_nes_chip[ChannelID::Noise] = NesChipID::NesApu2;
//    channel_to_nes_chip[ChannelID::Dpcm] = NesChipID::NesApu2;

    for (NesChipID chip_id : channel_to_nes_chip) {
        if (chip_id == NesChipID::COUNT) {
            throw std::logic_error(
                "Code error: channel without an associated NES chip!"
            );
        }
    }

    return channel_to_nes_chip;
}();

// namespace
}
}
