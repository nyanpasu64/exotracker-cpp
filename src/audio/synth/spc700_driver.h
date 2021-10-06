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

using spc700_synth::Spc700Synth;
class Spc700Driver;

class Spc700ChannelDriver {
    uint8_t _channel_id;

    // Volume 32 out of [-128..127] is an acceptable default.
    // 64 results in clipping when playing many channels at once.
    uint8_t _prev_volume = 0x20;
    doc::Chromatic _prev_note = 0;
    bool _note_playing = false;

    // TODO how to handle "no instrument" state?
    // A separate "unset" state wastes RAM in SPC export.
    std::optional<doc::InstrumentIndex> _prev_instr{};

public:
    Spc700ChannelDriver(uint8_t channel_id);

    /// When samples are edited, this gets called after APU has been reset.
    /// Writes current volume/etc. to the sound chip, but not currently playing note
    /// (since the sample has changed or moved).
    void restore_state(doc::Document const& document, RegisterWriteQueue & regs) const;

private:
    void write_volume(RegisterWriteQueue & regs) const;

public:
    // TODO make naming consistent (tick_tempo vs. sequencer_ticked).
    void run_driver(
        doc::Document const& document,
        Spc700Driver const& chip_driver,
        bool sequencer_ticked,
        EventsRef events,
        RegisterWriteQueue &/*mut*/ regs,
        Spc700ChipFlags & flags);
};

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

    explicit Spc700Driver(FrequenciesRef frequencies);
    DISABLE_COPY(Spc700Driver)
    DEFAULT_MOVE(Spc700Driver)

    /// Called when beginning playback from a clean slate.
    void reset_state(
        doc::Document const& document, Spc700Synth & synth, RegisterWriteQueue & regs
    );

private:
    // Only used in reset_state().
    Spc700Driver();

    /// When samples are edited, this gets called after APU has been reset.
    /// Reinitialize SPC700 and write current volume/etc. to the sound chip,
    /// but not currently playing notes (since the samples have changed or moved).
    void restore_state(
        doc::Document const& document, RegisterWriteQueue & regs
    ) const;

public:
    /// Called when samples are edited.
    void reload_samples(
        doc::Document const& document, Spc700Synth & synth, RegisterWriteQueue & regs
    );

    void stop_playback(RegisterWriteQueue /*mut*/& regs);

    void run_driver(
        doc::Document const& document,
        bool tick_tempo,
        EnumMap<ChannelID, EventsRef> const& channel_events,
        RegisterWriteQueue &/*mut*/ regs);

    friend class Spc700ChannelDriver;
};

}
