#pragma once

#include "music_driver_common.h"
#include "doc.h"
#include "chip_kinds.h"
#include "util/enum_map.h"

namespace audio::synth::spc700_driver {

using namespace music_driver;
using chip_kinds::Spc700ChannelID;
using doc::FrequenciesRef;

class Spc700ChannelDriver {
    // ???
};

class Spc700Driver {
private:
    Spc700ChannelDriver _channels[enum_count<Spc700ChannelID>];

public:
    using ChannelID = chip_kinds::Spc700ChannelID;

    Spc700Driver(ClockT clocks_per_sec, FrequenciesRef frequencies);
    DISABLE_COPY(Spc700Driver)
    DEFAULT_MOVE(Spc700Driver)

    void stop_playback(RegisterWriteQueue &/*mut*/ register_writes) {}

    void driver_tick(
        doc::Document const& document,
        EnumMap<ChannelID, EventsRef> const& channel_events,
        RegisterWriteQueue &/*mut*/ register_writes)
    {}
};

}
