#pragma once

#include "audio/synth_common.h"
#include "music_driver_common.h"
#include "doc.h"
#include "chip_kinds.h"
#include "util/enum_map.h"

namespace audio::synth::spc700 {
    // this class is friends with Spc700Driver, so we can load samples directly.
    class Spc700Synth;
}

namespace audio::synth::spc700_driver {

using namespace music_driver;
using chip_kinds::Spc700ChannelID;
using doc::FrequenciesRef;

class Spc700ChannelDriver {
    // ???
};

class Spc700Driver {
private:
    // TODO save the address of each sample
    Spc700ChannelDriver _channels[enum_count<Spc700ChannelID>];

    // Used to determine whether to attempt to play certain samples,
    // or avoid them and reject all notes using the sample.
    bool _samples_valid[doc::MAX_SAMPLES] = {false};

public:
    using ChannelID = chip_kinds::Spc700ChannelID;

    Spc700Driver(NsampT samples_per_sec, FrequenciesRef frequencies);
    DISABLE_COPY(Spc700Driver)
    DEFAULT_MOVE(Spc700Driver)

    void reload_samples(
        doc::Document const& document,
        spc700::Spc700Synth & synth,
        RegisterWriteQueue & register_writes);

    void stop_playback(RegisterWriteQueue /*mut*/& register_writes);

    void driver_tick(
        doc::Document const& document,
        EnumMap<ChannelID, EventsRef> const& channel_events,
        RegisterWriteQueue &/*mut*/ register_writes);
};

}
