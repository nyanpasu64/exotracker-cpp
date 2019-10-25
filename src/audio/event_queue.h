#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

namespace audio {

namespace event_queue {
using CycleT = uint32_t;
}

/**
Allocation-free min priority queue which schedules a finite set of events.
Used to find the first event (smallest timestamp) to happen now or in the future.

Template parameters:
- EventID must be an (old or new-style) enum with a final COUNT field.
  By convention, the first element is EndOfCallback.

If you use this as an object field in a callback class,
you can schedule events ("end of callback", tracker ticks, wavetable steps, etc.)
which persist across callback method calls.

The callback method can act like a state machine (or possibly coroutine)
which simulates executing 1 cycle at a time and handling events as they occur,
and suspending at arbitrary points in time (using EventID::EndOfCallback).

----

Generally, in the owner callback object's constructor:
- Enqueue (set_timeout()) recurring events like engine ticks

Every time the owner callback is called:
- call reset_now()
- Enqueue (set_timeout()) EndOfCallback after a known number of cycles
- Use an infinite loop to call next_event():
    - If EndOfCallback is returned, return.
    - For other events, perform processing and enqueue more events as needed.

An attempt for me to do the same thing as FamiTracker, but in an understandable way.
*/
template<typename EventID>
class EventQueue {
public:
    // Types
    using EventInt = std::underlying_type_t<EventID>;
    using CycleT = event_queue::CycleT;

    // private:
    // Fields
    static CycleT const NEVER = std::numeric_limits<CycleT>::max();
    CycleT time_until[EventID::COUNT];   // fill with NEVER

public:
    EventQueue() {
        for (auto & time : time_until) {
            time = NEVER;
        }
    }

    // Methods

    /**
    Schedules an event to happen now (t=0) or in the future.
    This event_id will be returned in a future call to next_event().

    If you call set_timeout on the same event ID twice without dequeueing the old one,
    the previous event schedule will be dropped.
    */
    void set_timeout(EventID event_id, CycleT in_how_long) {
        time_until[(EventInt) event_id] = in_how_long;
    }

    // TODO rename set_timeout to queue_event?
    // TODO add unqueue_event()? remove_event?

    struct RelativeEvent {
        EventID event_id;
        CycleT cyc_elapsed;
    };

    /**
    Finds the first event (smallest timestamp) to happen now or in the future.
    Returns (event ID, cycles since previous event returned by next_event()).
    The returned event is descheduled (moved to the end of time, or NEVER).

    If multiple events happen at the same time, the smallest event ID is returned.
    If no events are queued (all events occur at NEVER),
    the smallest event ID is returned (but you really shouldn't do this).
    */
    RelativeEvent next_event() {
        struct AbsoluteEvent {
            EventInt event_id;
            CycleT time;
        };

        // do I rewrite this in terms of some reduce function instead of a manual loop?
        AbsoluteEvent out;
        {
            EventInt event_id = 0;
            out = {event_id, time_until[event_id]};
        }

        for (EventInt event_id = 1; event_id < (EventInt) EventID::COUNT; event_id++) {
            if (time_until[event_id] < out.time) {
                out = {event_id, time_until[event_id]};
            }
        }

        time_until[out.event_id] = NEVER;

        advance_time(out.time);

        return RelativeEvent{(EventID) out.event_id, out.time};
    }

private:
    inline void advance_time(int dtime) {
        for (auto & time_cyc : time_until) {
            if (time_cyc != NEVER) {
                time_cyc -= dtime;
            }
        }
    }
};

// end namespace
}
