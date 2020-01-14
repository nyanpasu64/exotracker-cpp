#pragma once

#include "doc.h"
#include "util/enum_map.h"

#include <gsl/span>

#include <vector>

namespace audio::synth::sequencer {

using namespace chip_kinds;

using EventsRef = gsl::span<doc::RowEvent const>;

/// Why signed? Events can have negative offsets and play before their anchor beat,
/// or even before the owning pattern starts. This is a feature(tm).
using TickT = int32_t;

/// This struct can either represent a tick or delay.
///
/// FlattenedEventList._delay_events is std::vector<TickOrDelayEvent>.
///
/// FlattenedEventList::load_events_mut() first uses it to store ticks,
/// then converts it to delays before returning.
struct TickOrDelayEvent {
    TickT tick_or_delay;
    TickT & delay() {
        return tick_or_delay;
    }
    doc::RowEvent event;
};

using DelayEvent = TickOrDelayEvent;
using DelayEventsRefMut = gsl::span<DelayEvent>;

struct FlattenedEventList {
    // types
    struct EventListAndDuration {
        doc::EventList const & event_list;
        doc::BeatFraction nbeats;
    };

    // fields
    std::vector<TickOrDelayEvent> _delay_events;
    std::ptrdiff_t _next_event_idx;

    // TODO cache all inputs into load_events_mut(),
    // and add a method to increment cache.now().

    // impl
    FlattenedEventList() {
        // FamiTracker only supports 256 rows per channel. Who would fill them all?
        // 512 events ought to be enough for everybody!?
        // make that 1024 because i may add scripts which auto-generate
        // "note release" events before new notes begin
        _delay_events.reserve(1024);
    }

    /// returns mutable <'Self>.
    [[nodiscard]] DelayEventsRefMut load_events_mut(
        EventListAndDuration const pattern, doc::SequencerOptions options, TickT now
    );

    [[nodiscard]] DelayEventsRefMut get_events_mut();

    void pop_event();
};

/*
TODO only expose through unique_ptr?
(Unnecessary since class is not polymorphic. But speed hit remains.)

Moving only methods to .cpp is not helpful,
since ChannelSequencer's list of fields is in flux, and co-evolves with its methods.
*/
class ChannelSequencer {
    using EventsThisTickOwned = std::vector<doc::RowEvent>;
    EventsThisTickOwned _events_this_tick;

    // Idea: In ticked/timed code, never use "curr" in variable names.
    // Only ever use prev and next. This may reduce bugs, or not.
    int _next_tick = 0;

    /// TODO: Recomputed whenever next_tick() receives different EventList or parameters.
    /// For now, recomputed every tick.
    FlattenedEventList _event_cache;

public:
    // impl
    ChannelSequencer();

    /// TODO
    void seek() {}
    
    // Owning a vector, but returning a span, avoids the double-indirection of vector&.
    /// Eventually, (document, ChipIndex, ChannelIdInt) will be passed in as well.
    EventsRef next_tick(
        doc::Document const & document, ChipIndex chip_index, ChannelIndex chan_index
    );
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
        doc::Document const & document, ChipIndex chip_index
    ) {
        EnumMap<ChannelID, EventsRef> channel_events;

        for (ChannelIndex chan = 0; chan < enum_count<ChannelID>; chan++) {
            channel_events[chan] = _channel_sequencers[chan]
                .next_tick(document, chip_index, chan);
        }

        return channel_events;
    }
};

}
