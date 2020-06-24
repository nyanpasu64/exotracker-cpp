#pragma once

#include "doc.h"
#include "chip_common.h"
#include "timing_common.h"
#include "util/enum_map.h"
#include "util/release_assert.h"
#include "util/compare.h"

#include <gsl/span>

#include <tuple>
#include <vector>

namespace audio::synth::sequencer {

using namespace chip_common;
using timing::SequencerTime;

using EventsRef = gsl::span<doc::RowEvent const>;
using EventIndex = uint32_t;

/// Why signed? Events can have negative offsets and play before their anchor beat,
/// or even before the owning pattern starts. This is a feature(tm).
using TickT = int32_t;

struct BeatPlusTick {
    int32_t beat;
    int32_t dtick;

    COMPARABLE(BeatPlusTick, (beat, dtick))

    BeatPlusTick & operator+=(BeatPlusTick const & other) {
        beat += other.beat;
        dtick += other.dtick;
        return *this;
    }

    BeatPlusTick & operator-=(BeatPlusTick const & other) {
        beat -= other.beat;
        dtick -= other.dtick;
        return *this;
    }
};

struct RealTime {
    doc::SeqEntryIndex seq_entry = 0;
    // Idea: In ticked/timed code, never use "curr" in variable names.
    // Only ever use prev and next. This may reduce bugs, or not.
    BeatPlusTick next_tick = {0, 0};
};

struct EventIterator {
    // Not read yet, but will be used to handle document mutations in the future.
    doc::MaybeSeqEntryIndex prev_seq_entry = {};

    doc::SeqEntryIndex seq_entry = 0;
    EventIndex event_idx = 0;
};

/// How many patterns EventIterator is ahead of RealTime.
/// Should be 0 within patterns, 1 if we're playing notes delayed past a pattern,
/// and -1 if we're playing notes pushed before a pattern.
///
/// You can't just compare sequence entry numbers, because of looping.
class PatternOffset {
    int _event_minus_now = 0;

public:
    using Success = bool;

    /// you really shouldn't need this, but whatever.
    auto event_minus_now() const {
        return _event_minus_now;
    }

    Success advance_event() {
        if (event_is_ahead()) {
            return false;
        }
        _event_minus_now += 1;
        return true;
    }

    Success advance_now() {
        if (event_is_behind()) {
            return false;
        }
        _event_minus_now -= 1;
        return true;
    }

    bool event_is_ahead() const {
        return _event_minus_now > 0;
    }

    bool event_is_behind() const {
        return _event_minus_now < 0;
    }
};

// This is UB. (shrug)
#ifndef ChannelSequencer_INTERNAL
#define ChannelSequencer_INTERNAL private
#endif

/*
TODO only expose through unique_ptr?
(Unnecessary since class is not polymorphic. But speed hit remains.)

Moving only methods to .cpp is not helpful,
since ChannelSequencer's list of fields is in flux, and co-evolves with its methods.
*/
class ChannelSequencer {
ChannelSequencer_INTERNAL:
    // Must be assigned after construction.
    ChipIndex _chip_index = (ChipIndex) -1;
    ChannelIndex _chan_index = (ChannelIndex) -1;

    using EventsThisTickOwned = std::vector<doc::RowEvent>;
    EventsThisTickOwned _events_this_tick;

    /// Time in document. Used for playback, and possibly GUI scrolling.
    /// Mutations are not affected by _next_event.
    RealTime _now;

    /// Next event in document to be played.
    /// May not even be in the same pattern as _now.
    /// Mutations are affected by _now.
    EventIterator _next_event;

    /// Track whether EventIterator is at an earlier/later pattern than RealTime.
    PatternOffset _pattern_offset;

public:
    // impl
    ChannelSequencer();

    void set_chip_chan(ChipIndex chip_index, ChannelIndex chan_index) {
        _chip_index = chip_index;
        _chan_index = chan_index;
    }

    void seek(doc::Document const & document, SequencerTime time);

    /// Owning a vector, but returning a span, avoids the double-indirection of vector&.
    /// Return: SequencerTime is current tick (just occurred), not next tick.
    std::tuple<SequencerTime, EventsRef> next_tick(doc::Document const & document);
};

/// The sequencer owned by a (Chip)Instance.
/// ChannelID is an enum of that chip's channels, followed by COUNT.
template<typename ChannelID>
class ChipSequencer {
    EnumMap<ChannelID, ChannelSequencer> _channel_sequencers;

public:
    // impl
    ChipSequencer(ChipIndex chip_index) {
        for (ChannelIndex chan = 0; chan < enum_count<ChannelID>; chan++) {
            _channel_sequencers[chan].set_chip_chan(chip_index, chan);
        }
    }

    void seek(doc::Document const & document, SequencerTime time) {
        for (ChannelIndex chan = 0; chan < enum_count<ChannelID>; chan++) {
            _channel_sequencers[chan].seek(document, time);
        }
    }

    std::tuple<SequencerTime, EnumMap<ChannelID, EventsRef>> sequencer_tick(
        doc::Document const & document
    ) {
        EnumMap<ChannelID, EventsRef> channel_events;

        SequencerTime seq_chip_time;
        static_assert(enum_count<ChannelID> > 0, "invalid chip with 0 channels");

        for (ChannelIndex chan = 0; chan < enum_count<ChannelID>; chan++) {
            auto [seq_chan_time, events] =
                _channel_sequencers[chan].next_tick(document);

            // Get audio position.
            if (chan > 0) {
                // TODO should this be release_assert?
                assert(seq_chip_time == seq_chan_time);
            }
            seq_chip_time = seq_chan_time;

            // Get events.
            channel_events[chan] = events;
        }

        return {seq_chip_time, channel_events};
    }
};

}
