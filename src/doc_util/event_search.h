#pragma once

#include "doc/event_list.h"

#include <optional>
#include <type_traits>

namespace doc_util::event_search {

using namespace doc::timed_events;
using namespace doc::event_list;

namespace detail {
    /// These methods can be used to search through both const and mutable _event_list.
    template<typename EventsT, typename ConstIterator_>
    class EventSearchTmp {
    protected:
        EventsT _event_list;

    public:
        EventSearchTmp(EventsT event_list) : _event_list{event_list} {}

        using This = EventSearchTmp;
        using ConstIterator = ConstIterator_;

        [[nodiscard]] ConstIterator greater_equal(TimeInPattern t) const;
        [[nodiscard]] ConstIterator greater(TimeInPattern t) const;

        [[nodiscard]] ConstIterator beat_begin(BeatFraction beat) const;
        [[nodiscard]] ConstIterator beat_end(BeatFraction beat) const;
    };
}


/// Wrapper for TimedEventsRef (immutable span), adding the ability to binary-search.
using EventSearch = detail::EventSearchTmp<TimedEventsRef, TimedEventsRef::iterator>;


/// Not for external use.
using EventSearchMutBase =
    detail::EventSearchTmp<EventList &, EventList::const_iterator>;


/// Mutable-reference wrapper for EventList,
/// adding the ability to binary-search and insert events.
class EventSearchMut : public EventSearchMutBase {
public:
    // EventSearchMut()
    using EventSearchMutBase::EventSearchMutBase;

    using Iterator = EventList::iterator;

    Iterator greater_equal(TimeInPattern t);
    Iterator greater(TimeInPattern t);

    Iterator beat_begin(BeatFraction beat);
    Iterator beat_end(BeatFraction beat);

    /// Returns reference to last event anchored to this beat fraction.
    /// Inserts new event if none exist at this time.
    TimedRowEvent & get_or_insert(BeatFraction beat);
};

}
