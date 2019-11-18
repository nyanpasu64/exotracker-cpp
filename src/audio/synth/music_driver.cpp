#include "music_driver.h"

#include "music_driver/nes_2a03.h"

#include <cassert>

namespace audio {
namespace synth {
namespace music_driver {

OverallMusicDriver::OverallMusicDriver() {
#define INITIALIZE(key)  _channel_drivers[ChannelID::key] = nes_2a03::make_ ## key();
    INITIALIZE(Pulse1)
    INITIALIZE(Pulse2)
//    INITIALIZE(Tri)
//    INITIALIZE(Noise)
//    INITIALIZE(Dpcm)

    for (auto & driver : _channel_drivers) {
        assert(driver != nullptr);
    }
}

using sequencer::EventsThisTickRef;

void OverallMusicDriver::get_frame_registers(
    ChipRegisterWriteQueue & chip_register_writes
) {
    for (size_t chan = 0; chan < enum_count<ChannelID>; chan++) {
        NesChipID chip = synth::CHANNEL_TO_NES_CHIP[chan];

        SubMusicDriver & sub_driver = *_channel_drivers[chan];
        RegisterWriteQueue & reg_writes = chip_register_writes[chip];
        EventsThisTickRef tick_events = _channel_sequencers[chan].next_tick();

        // All register writes will be at time 0 for simplicity, for the time being.
        sub_driver.run(reg_writes, tick_events);
    }
}

// end namespaces
}
}
}
