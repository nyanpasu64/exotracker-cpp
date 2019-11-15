#include "music_engine.h"

#include "music_engine/nes_2a03.h"

#include <cassert>

namespace audio {
namespace synth {
namespace music_engine {

OverallMusicEngine::OverallMusicEngine() {
#define INITIALIZE(key)  _channel_engines[ChannelID::key] = nes_2a03::make_ ## key();
    INITIALIZE(Pulse1)
    INITIALIZE(Pulse2)
//    INITIALIZE(Tri)
//    INITIALIZE(Noise)
//    INITIALIZE(Dpcm)

    for (auto & engine : _channel_engines) {
        assert(engine != nullptr);
    }
}

void OverallMusicEngine::get_frame_registers(
    ChipRegisterWriteQueue & chip_register_writes
) {
    for (size_t chan = 0; chan < enum_count<ChannelID>; chan++) {
        NesChipID chip = synth::CHANNEL_TO_NES_CHIP[chan];

        SubMusicEngine & sub_engine = *_channel_engines[chan];
        RegisterWriteQueue & reg_writes = chip_register_writes[chip];

        // All register writes will be at time 0 for simplicity, for the time being.
        sub_engine.run(reg_writes);
    }
}

// end namespaces
}
}
}
