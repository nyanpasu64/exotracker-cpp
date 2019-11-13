#pragma once

#include "music_engine_common.h"
#include "../synth_common.h"
#include "util/enum_map.h"

#include <memory>

namespace audio {
namespace synth {
namespace music_engine {

/// (sound, audio, music, playback) (engine, driver)
class OverallMusicEngine {
    EnumMap<ChannelID, std::unique_ptr<SubMusicEngine>> _channel_engines;

public:
    OverallMusicEngine();

    void get_frame_registers(ChipRegisterWriteQueue & chip_register_writes);
};

// end namespaces
}
}
}