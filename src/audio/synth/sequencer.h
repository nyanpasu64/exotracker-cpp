#pragma once

#include "document.h"
#include "util/enum_map.h"

#include <gsl/span>

#include <vector>
#include <cstdlib>

namespace audio::synth::sequencer {

using namespace chip_kinds;

using EventsRef = gsl::span<doc::RowEvent>;

/*
TODO only expose through unique_ptr?
(Unnecessary since class is not polymorphic. But speed hit remains.)

Moving only methods to .cpp is not helpful,
since ChannelSequencer's list of fields is in flux, and co-evolves with its methods.
*/
class ChannelSequencer {
    using EventsThisTickOwned = std::vector<doc::RowEvent>;
    EventsThisTickOwned _events_this_tick;
    int time_until_toggle = 30 + rand() % 90;

public:
    // impl
    ChannelSequencer() {
        /*
        On ticks without events, ChannelSequencer should return a 0-length vector.
        On ticks with events, ChannelSequencer should return a 1-length vector.

        The only time we should return more than 1 event is with broken documents,
        where multiple events occur at the same time
        (usually due to early events being offset later,
        or later events being offset earlier).

        Later events prevent earlier events from being offset later;
        instead they will pile up at the same time as the later event.

        We should never reach or exceed 4 events simultaneously.
        */
        _events_this_tick.reserve(4);
    }

    /// TODO
    void seek() {}
    
    // Owning a vector, but returning a span, avoids the double-indirection of vector&.
    /// Eventually, (document, ChipIndex, ChannelIdInt) will be passed in as well.
    EventsRef next_tick(
        doc::Document & document, ChipIndex chip_index, ChannelIndex chan_index
    ) {
        _events_this_tick.clear();

        if (time_until_toggle == 0) {
            // Yield an event.
            std::optional<doc::Note> note;
            // note = {} refers to "row without note". I want "random note cuts", but that will come later.
            note = {60};
            _events_this_tick = {doc::RowEvent{note}};

            // Queue next event.
            time_until_toggle = 30 + rand() % 90;
        }

        // Advance time.
        assert(time_until_toggle > 0);
        time_until_toggle -= 1;

        return _events_this_tick;
    }
};

/// The sequencer owned by a (Chip)Instance.
/// ChannelID is an enum of that chip's channels, followed by COUNT.
template<typename ChannelID>
class ChipSequencer {
    EnumMap<ChannelID, ChannelSequencer> _channel_sequencers;

public:
    // impl

    /// Eventually, (document, ChipIndex) will be passed in as well.
    EnumMap<ChannelID, EventsRef> sequencer_tick(
        doc::Document & document, ChipIndex chip_index
    ) {
        EnumMap<ChannelID, EventsRef> channel_events;

        for (size_t chan = 0; chan < enum_count<ChannelID>; chan++) {
            channel_events[chan] = _channel_sequencers[chan]
                .next_tick(document, chip_index, chan);
        }

        return channel_events;
    }
};

}
