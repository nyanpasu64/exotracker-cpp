#pragma once

#include "./sequencer.h"
#include "chip_instance_common.h"
#include "timing_common.h"
#include "util/enum_map.h"

#include <utility>  // std::move

namespace audio::synth::impl_chip {

using std::move;
using chip_instance::ChipInstance;
using chip_instance::RegisterWrite;
using chip_common::ChipIndex;
using timing::SequencerTime;

template<
    typename DriverT,
    typename SynthT>
class ImplChipInstance : public ChipInstance {
// fields
    using ChannelID = typename DriverT::ChannelID;

    // ChipSequencer::sequencer_tick() returns EnumMap<ChannelID, EventsRef>.
    sequencer::ChipSequencer<ChannelID> _chip_sequencer;

    // DriverT::run_driver() takes EnumMap<ChannelID, EventsRef>.
    DriverT _driver;

    /*
    Not statically verified to belong to the same ChannelID.

    It's useful to use the same SynthT for multiple ChannelID,
    since 4-op FM may have two different ChannelID types (unified/split ch3),
    and FDS may have two different ChannelID types (1 or 2 channels).

    On the other hand, N163 has a variable number of channels,
    and it's impractical to create new ChannelID each time.
    */
    SynthT _synth;

// impl
public:
    ImplChipInstance(
        ChipIndex chip_index,
        DriverT driver,
        SynthT synth)

        : ChipInstance()
        , _chip_sequencer(chip_index)
        , _driver(move(driver))
        , _synth(move(synth))
    {}

    // impl ChipInstance
    void seek(doc::Document const& document, timing::GridAndBeat time) override {
        _chip_sequencer.seek(document, time);
    }

    void ticks_per_beat_changed(doc::Document const& document) override {
        _chip_sequencer.ticks_per_beat_changed(document);
    }

    void doc_edited(doc::Document const& document) override {
        _chip_sequencer.doc_edited(document);
    }

    void timeline_modified(doc::Document const& document) override {
        _chip_sequencer.timeline_modified(document);
    }

    void reset_state(doc::Document const& document) override {
        _driver.reset_state(document, /*mut*/ _synth, /*mut*/ _register_writes);
    }

    void reload_samples(doc::Document const& document) override {
        _driver.reload_samples(document, /*mut*/ _synth, /*mut*/ _register_writes);
    }

    void stop_playback() override {
        _chip_sequencer.stop_playback();
        _driver.stop_playback(/*mut*/ _register_writes);
    }

    SequencerTime sequencer_driver_tick(doc::Document const& document) override {
        auto [chip_time, channel_events] = _chip_sequencer.sequencer_tick(document);

        _driver.driver_tick(document, channel_events, /*mut*/ _register_writes);

        return chip_time;
    }

    void driver_tick(doc::Document const& document) override {
        _driver.driver_tick(document, {}, /*mut*/ _register_writes);
    }

private:  // called by ChipInstance
    void synth_write_reg(RegisterWrite write) override {
        _synth.write_reg(write);
    }

    NsampWritten synth_run_clocks(
        ClockT const nclk,
        WriteTo write_to)
    override {
        return _synth.run_clocks(nclk, write_to);
    }
};

}
