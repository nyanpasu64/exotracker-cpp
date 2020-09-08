#pragma once

#include "doc/event_list.h"

#include <optional>
#include <type_traits>

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

    [[nodiscard]] ConstIterator greater_equal(TimeInPattern t) const;
    [[nodiscard]] ConstIterator greater(TimeInPattern t) const;

    [[nodiscard]] ConstIterator beat_begin(BeatFraction beat) const;
    [[nodiscard]] ConstIterator beat_end(BeatFraction beat) const;
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
    [[nodiscard]] ConstIterator greater_equal(TimeInPattern t) const;
    [[nodiscard]] ConstIterator greater(TimeInPattern t) const;

    [[nodiscard]] ConstIterator beat_begin(BeatFraction beat) const;
    [[nodiscard]] ConstIterator beat_end(BeatFraction beat) const;

    // Mutating methods.
    [[nodiscard]] Iterator greater_equal(TimeInPattern t);
    [[nodiscard]] Iterator greater(TimeInPattern t);

    [[nodiscard]] Iterator beat_begin(BeatFraction beat);
    [[nodiscard]] Iterator beat_end(BeatFraction beat);

    /// Returns reference to last event anchored to this beat fraction.
    /// Inserts new event if none exist at this time.
    [[nodiscard]] TimedRowEvent & get_or_insert(BeatFraction beat);
};

}
