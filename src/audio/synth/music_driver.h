#pragma once

#include "music_driver_common.h"
#include "../synth_common.h"
#include "sequencer/sequencer.h"
#include "util/enum_map.h"

#include <memory>

namespace audio {
namespace synth {
namespace music_driver {

/// (sound, audio, music, playback) (driver, driver)
class OverallMusicDriver {
    EnumMap<ChannelID, std::unique_ptr<SubMusicDriver>> _channel_drivers;
    EnumMap<ChannelID, sequencer::ChannelSequencer> _channel_sequencers;

public:
    OverallMusicDriver();

    void get_frame_registers(ChipRegisterWriteQueue & chip_register_writes);
};

// end namespaces
}
}
}
