#pragma once

#include "doc/event_list.h"

#include <optional>

namespace edit_util::kv {

using namespace doc::timed_events;
using namespace doc::event_list;

/// Mutable-reference wrapper for EventList (I wish C++ had extension methods),
/// adding the ability to binary-search and treat it as a map.
class KV {
    EventList & _event_list;

public:
    KV(EventList & event_list) : _event_list{event_list} {}

    using This = KV;
    using Iterator = EventList::iterator;
    using ConstIterator = EventList::const_iterator;

    ConstIterator greater_equal(TimeInPattern t) const;
    Iterator greater_equal(TimeInPattern t);

    ConstIterator greater(TimeInPattern t) const;
    Iterator greater(TimeInPattern t);

    ConstIterator beat_begin(BeatFraction beat) const;
    Iterator beat_begin(BeatFraction beat);

    ConstIterator beat_end(BeatFraction beat) const;
    Iterator beat_end(BeatFraction beat);

    /// Returns reference to last event anchored to this beat fraction.
    /// Inserts new event if none exist at this time.
    TimedRowEvent & get_or_insert(BeatFraction beat);
};

}