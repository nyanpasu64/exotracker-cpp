#pragma once

#include "doc.h"
#include "doc_util/track_util.h"
#include "sequencer_driver_common.h"
#include "chip_common.h"
#include "timing_common.h"
#include "util/enum_map.h"
#include "util/release_assert.h"

#include <tuple>
#include <vector>

namespace audio::synth::sequencer {

// This is UB. (shrug)
#ifndef sequencer_INTERNAL
#define sequencer_INTERNAL private
#endif

using namespace chip_common;
using timing::SequencerTime;

using sequencer_driver::EventsRef;
using doc::EventIndex;
using doc::TickT;
using doc_util::track_util::TrackPatternIter;

/// Like doc::PatternRef but doesn't hold a persistent reference to the document
/// (which will dangle if parts of the document are replaced during mutation).
struct PatternIndex {
    doc::BlockIndex block;
    // int loop;

    /// Timestamps within document.
    TickT begin_tick{};
    TickT end_tick{};

    static PatternIndex from(doc::PatternRef pattern) {
        return PatternIndex {
            .block = pattern.block,
            .begin_tick = pattern.begin_tick,
            .end_tick = pattern.end_tick,
        };
    }
};

class EventIterator {
sequencer_INTERNAL:
    /// If song ended or track empty, points to no pattern.
    TrackPatternIter _patterns;

    /// May point past the end of _patterns.peek()->events.
    EventIndex _event_idx;

public:
    /// Ignores Gxx ±tick offsets by design; may return a tick anchored >= now but
    /// playing before now. It should be played immediately.
    static EventIterator at_time(doc::SequenceTrackRef track, TickT now);
};

/*
Idea: only expose through unique_ptr?
(Unnecessary since class is not polymorphic. But speed hit remains.)

Moving only methods to .cpp does not reduce rebuilds,
since ChannelSequencer's list of fields is in flux, and co-evolves with its methods.
*/
class ChannelSequencer {
sequencer_INTERNAL:
// types
    using EventsThisTickOwned = std::vector<doc::RowEvent>;

// fields
    // Must be assigned after construction.
    ChipIndex _chip_index = (ChipIndex) -1;
    ChannelIndex _chan_index = (ChannelIndex) -1;

    EventsThisTickOwned _events_this_tick;

    /// Time in document as of *next* tick. Used for playback, and possibly GUI
    /// scrolling. Mutations are not affected by _curr_event.
    TickT _now;

    bool _ignore_ordering_errors;

//    /// Truncated/quantized time. (TODO implement later on, my squishy meat brain
//    /// can't handle it)
//    TickT _note_qticks_left;

    /// Next event in document to be played (>= _now).
    /// - If not playing, nullopt.
    /// - After last event in pattern, ->_event_idx == pattern.events.size().
    /// - After last block in song, ->_patterns.peek() == nullopt.
    std::optional<EventIterator> _curr_pattern_next_ev;

public:
    // impl
    ChannelSequencer();

    void set_chip_chan(ChipIndex chip_index, ChannelIndex chan_index) {
        _chip_index = chip_index;
        _chan_index = chan_index;
    }

    /// Sets _now to 0, and _curr_pattern_next_ev ("is playing") to nullopt.
    ///
    /// Postconditions:
    /// - not playing (_curr_pattern_next_ev == nullopt)
    void stop_playback();

    /// Recompute _now based on timestamp.
    /// Recompute _next_event based on document and timestamp.
    /// Doesn't matter if document was edited or not.
    ///
    /// Preconditions:
    /// - None. This resets sequencer state, so the previous/current document
    ///   don't need to be the same.
    ///
    /// Postconditions:
    /// - playing (_curr_pattern_next_ev != nullopt)
    void seek(doc::Document const & document, TickT time);

    /// Recompute _next_event based on _now and edited document.
    ///
    /// Preconditions:
    /// - playing (_curr_pattern_next_ev != nullopt)
    ///   - if not playing, this function does nothing
    void doc_edited(doc::Document const & document);

    // next_tick() is declared last in the header, but implemented first in the .cpp.
    /// Owning a vector, but returning a span, avoids the double-indirection of vector&.
    ///
    /// Preconditions:
    /// - playing (_curr_pattern_next_ev != nullopt)
    /// - Document is unchanged, or else doc_edited() has been called.
    ///
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

    void stop_playback() {
        for (ChannelIndex chan = 0; chan < enum_count<ChannelID>; chan++) {
            _channel_sequencers[chan].stop_playback();
        }
    }

    void seek(doc::Document const & document, TickT time) {
        for (ChannelIndex chan = 0; chan < enum_count<ChannelID>; chan++) {
            _channel_sequencers[chan].seek(document, time);
        }
    }

    void doc_edited(doc::Document const & document) {
        for (ChannelIndex chan = 0; chan < enum_count<ChannelID>; chan++) {
            _channel_sequencers[chan].doc_edited(document);
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
