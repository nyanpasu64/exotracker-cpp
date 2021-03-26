#pragma once

#include "audio/synth_common.h"
#include "music_driver_common.h"
#include "doc.h"
#include "chip_kinds.h"
#include "util/enum_map.h"

#include <cstdint>

namespace audio::synth::spc700_synth {
    // this class is friends with Spc700Driver, so we can load samples directly.
    class Spc700Synth;
}

namespace audio::synth::spc700_driver {

using namespace music_driver;
using chip_kinds::Spc700ChannelID;
using namespace doc::tuning;

struct Spc700ChipFlags {
    /// If any bits are set, then the value is written to the S-DSP's KON register,
    /// retriggering the corresponding channels.
    uint8_t kon = 0;

    /// If any bits are set, then the value is written to the S-DSP's KOFF register,
    /// releasing the corresponding channels.
    uint8_t koff = 0;
};

class Spc700Driver;

class Spc700ChannelDriver {
    uint8_t _channel_id;

    doc::Note _prev_note = 0;
    bool _note_playing = false;

    // TODO how to handle "no instrument" state?
    // A separate "unset" state wastes RAM in SPC export.
    std::optional<doc::InstrumentIndex> _prev_instr{};

public:
    Spc700ChannelDriver(uint8_t channel_id);

    void tick(
        doc::Document const& document,
        Spc700Driver const& chip_driver,
        EventsRef events,
        RegisterWriteQueue &/*mut*/ register_writes,
        Spc700ChipFlags & flags);
};

using spc700_synth::Spc700Synth;

class Spc700Driver {
private:
    // TODO save the address of each sample
    Spc700ChannelDriver _channels[enum_count<Spc700ChannelID>];

    /// Every instrument has its own tuning system, so compute tuning at runtime.
    FrequenciesOwned _freq_table;

    // Used to determine whether to attempt to play certain samples,
    // or avoid them and reject all notes using the sample.
    bool _samples_valid[doc::MAX_SAMPLES] = {false};

public:
    using ChannelID = chip_kinds::Spc700ChannelID;

    Spc700Driver(NsampT samples_per_sec, FrequenciesRef frequencies);
    DISABLE_COPY(Spc700Driver)
    DEFAULT_MOVE(Spc700Driver)

    // RegisterWriteQueue is currently unused.
    void reset_state(
        doc::Document const& document,
        Spc700Synth & synth,
        RegisterWriteQueue & register_writes);

    // RegisterWriteQueue is currently unused.
    void reload_samples(
        doc::Document const& document,
        Spc700Synth & synth,
        RegisterWriteQueue & register_writes);

    void stop_playback(RegisterWriteQueue /*mut*/& register_writes);

    void driver_tick(
        doc::Document const& document,
        EnumMap<ChannelID, EventsRef> const& channel_events,
        RegisterWriteQueue &/*mut*/ register_writes);

    friend class Spc700ChannelDriver;
};

}
