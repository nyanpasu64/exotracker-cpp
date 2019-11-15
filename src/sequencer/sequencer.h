#pragma once

#include "document.h"

#include <gsl/span>

#include <vector>
#include <cstdlib>

namespace sequencer {

using EventsThisTickRef = gsl::span<doc::RowEvent>;

/*
TODO only expose through unique_ptr?
(Unnecessary since class is not polymorphic. But speed hit remains.)

Moving only methods to .cpp is not helpful,
since ChannelSequencer's list of fields is in flux, and co-evolves with its methods.
*/
class ChannelSequencer {
    using EventsThisTickOwned = std::vector<doc::RowEvent>;
    EventsThisTickOwned _events_this_tick;
    int time_until_toggle = 0;

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
    EventsThisTickRef next_tick() {
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

}
