#pragma once

#include "doc/event_list.h"

namespace doc_util::event_search {

using namespace doc::timed_events;
using namespace doc::event_list;


/// Wrapper for TimedEventsRef (immutable span), adding the ability to binary-search.
class EventSearch {
    // types
public:
    using EventsT = TimedEventsRef;
    using ConstIterator = TimedEventsRef::iterator;

    // fields
private:
    EventsT _event_list;

    // impl
public:
    EventSearch(EventsT event_list) : _event_list{event_list} {}

    [[nodiscard]] ConstIterator tick_begin(TickT beat) const;
    [[nodiscard]] ConstIterator tick_end(TickT beat) const;
};


/// Mutable-reference wrapper for EventList,
/// adding the ability to binary-search and insert events.
class EventSearchMut {
    // types
public:
    using EventsT = EventList &;
    using ConstIterator = EventList::const_iterator;
    using Iterator = EventList::iterator;

    // fields
private:
    EventsT _event_list;

    // impl
public:
    EventSearchMut(EventsT event_list) : _event_list{event_list} {}

    // Const methods.
    [[nodiscard]] ConstIterator tick_begin(TickT beat) const;
    [[nodiscard]] ConstIterator tick_end(TickT beat) const;

    // Mutating methods.
    [[nodiscard]] Iterator tick_begin(TickT beat);
    [[nodiscard]] Iterator tick_end(TickT beat);

    /// Returns pointer to last event anchored to this beat fraction.
    /// Returns nullptr if none exist at this time.
    [[nodiscard]] TimedRowEvent * get_maybe(TickT beat);

    /// Returns reference to last event anchored to this beat fraction.
    /// Inserts new event if none exist at this time.
    [[nodiscard]] TimedRowEvent & get_or_insert(TickT beat);
};

}
