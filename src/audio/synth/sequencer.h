#pragma once

#include "doc.h"
#include "doc_util/track_util.h"
#include "sequencer_driver_common.h"
#include "chip_common.h"
#include "timing_common.h"
#include "util/enum_map.h"
#include "util/release_assert.h"
#include "util/compare.h"

#include <tuple>
#include <vector>

namespace audio::synth::sequencer {

using namespace chip_common;
using timing::SequencerTime;
using timing::GridAndBeat;

using sequencer_driver::EventsRef;
using doc::EventIndex;

/// Why signed? Events can have negative offsets and play before their anchor beat,
/// or even before the owning pattern starts. This is a feature(tm).
using TickT = int32_t;

struct BeatPlusTick {
    int32_t beat;
    int32_t dtick;

    COMPARABLE(BeatPlusTick)

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
    doc::GridIndex grid{0};
    // Idea: In ticked/timed code, never use "curr" in variable names.
    // Only ever use prev and next. This may reduce bugs, or not.
    BeatPlusTick next_tick = {0, 0};
};

using doc_util::track_util::FramePatternIter;

/// Like doc::PatternRef but doesn't hold a persistent reference to the document
/// (which will dangle if parts of the document are replaced during mutation).
struct PatternIndex {
    doc::BlockIndex block;
    // int loop;

    /// Timestamps within the current grid cell.
    doc::BeatIndex begin_time{};
    doc::BeatFraction end_time{};

    /// This pattern loop, only play the first `num_events` events.
    EventIndex num_events;

    static PatternIndex from(doc::PatternRef pattern) {
        return PatternIndex {
            .block = pattern.block,
            .begin_time = pattern.begin_time,
            .end_time = pattern.end_time,
            .num_events = (EventIndex) pattern.events.size(),
        };
    }
};

struct EventIterator {
    /*
    Assume we're past the last event in a grid cell, so event is past now.
    Then an event is added on the "now" grid cell.
    So we need to backtrack to the "now" grid cell (prev_grid).

    Should we backtrack by 1 grid or 1 block?
    Whichever we choose, we need to properly update GridRunahead,
    which tracks whether either of EventIterator or RealTime is ahead in grid cells.

    Reverting EventIterator to the previous grid cell will always revert
    GridRunahead's event iterator by 1 grid.

    Reverting EventIterator to the previous block may or may not revert GridRunahead,
    so we need to store a separate "now grid advanced from prev grid" field
    that is true even for looping documents.

    Another factor is that adding/removing blocks from a grid can corrupt `block`,
    just like it corrupts `event_idx`. So we should reset block to 0.

    The FramePatternIter abstraction means that we no longer care about blocks at all!
    */

    doc::MaybeGridIndex prev_grid = {};

    doc::GridIndex grid{0};
    std::optional<FramePatternIter> pattern_iter{};

    /// If current and next grid cell are empty, don't hold onto a pattern.
    std::optional<PatternIndex> pattern = {};
    EventIndex event_idx = 0;
};

/// How many grid cells EventIterator is ahead of RealTime.
/// Should be 0 within a cell, 1 if we're playing notes delayed past a gridline,
/// and -1 if we're playing notes pushed before a gridline.
///
/// You can't just compare gridline indexes, because of looping.
class GridRunahead {
    int _event_minus_now = 0;

    // impl
public:
    using Success = bool;

    /// you really shouldn't need this, but whatever.
    [[nodiscard]] int event_minus_now() const {
        return _event_minus_now;
    }

    [[nodiscard]] Success advance_event_grid() {
        if (event_grid_ahead()) {
            return false;
        }
        _event_minus_now += 1;
        return true;
    }

    [[nodiscard]] Success advance_now_grid() {
        if (event_grid_behind()) {
            return false;
        }
        _event_minus_now -= 1;
        return true;
    }

    [[nodiscard]] bool event_grid_ahead() const {
        return _event_minus_now > 0;
    }

    [[nodiscard]] bool event_grid_behind() const {
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

    /// "is playing" = (_curr_ticks_per_beat != 0).
    TickT _curr_ticks_per_beat;

    bool _ignore_ordering_errors;

    /// Next event in document to be played.
    /// May not even be in the same pattern as _now.
    /// Mutations are affected by _now.
    EventIterator _next_event;

    /// Track whether EventIterator is at an earlier/later grid cell than RealTime.
    GridRunahead _grid_runahead;

public:
    // impl
    ChannelSequencer();

    void set_chip_chan(ChipIndex chip_index, ChannelIndex chan_index) {
        _chip_index = chip_index;
        _chan_index = chan_index;
    }

    /// Sets _now and _curr_ticks_per_beat to 0.
    ///
    /// Postconditions:
    /// - not playing (_curr_ticks_per_beat == 0)
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
    /// - playing (_curr_ticks_per_beat != 0)
    void seek(doc::Document const & document, GridAndBeat time);

    /// Recompute _now based on timestamp and document tempo. Ignores events entirely.
    /// Can be called before doc_edited() if both tempo and events edited.
    ///
    /// Preconditions:
    /// - playing (_curr_ticks_per_beat != 0)
    void ticks_per_beat_changed(doc::Document const & document);

    /// Recompute _next_event based on _now and edited document.
    /// If tempo has changed, call ticks_per_beat_changed() beforehand.
    ///
    /// Preconditions:
    /// - playing (_curr_ticks_per_beat != 0)
    /// - ticks_per_beat unchanged from previous call to seek/ticks_per_beat_changed.
    /// - number and duration of timeline cells unmodified.
    void doc_edited(doc::Document const & document);

    /// Called when number or duration of timeline cells are changed.
    /// Force the cursor in-bounds (grid, then beat+tick), then recompute _next_event.
    /// If tempo has changed, call ticks_per_beat_changed() beforehand.
    ///
    /// Preconditions:
    /// - playing (_curr_ticks_per_beat != 0)
    /// - ticks_per_beat unchanged from previous call to seek/ticks_per_beat_changed.
    void timeline_modified(doc::Document const & document);

    // next_tick() is declared last in the header, but implemented first in the .cpp.
    /// Owning a vector, but returning a span, avoids the double-indirection of vector&.
    ///
    /// Preconditions:
    /// - playing (_curr_ticks_per_beat != 0)
    /// - Document is unchanged, or else appropriate methods have been called.
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

    void seek(doc::Document const & document, GridAndBeat time) {
        for (ChannelIndex chan = 0; chan < enum_count<ChannelID>; chan++) {
            _channel_sequencers[chan].seek(document, time);
        }
    }

    void ticks_per_beat_changed(doc::Document const & document) {
        for (ChannelIndex chan = 0; chan < enum_count<ChannelID>; chan++) {
            _channel_sequencers[chan].ticks_per_beat_changed(document);
        }
    }

    void doc_edited(doc::Document const & document) {
        for (ChannelIndex chan = 0; chan < enum_count<ChannelID>; chan++) {
            _channel_sequencers[chan].doc_edited(document);
        }
    }

    void timeline_modified(doc::Document const & document) {
        for (ChannelIndex chan = 0; chan < enum_count<ChannelID>; chan++) {
            _channel_sequencers[chan].timeline_modified(document);
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
