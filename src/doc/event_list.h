#pragma once

#include "events.h"
#include "timed_events.h"

#include <vector>

namespace doc::event_list {

using events::RowEvent;
using timed_events::TimeInPattern;
using timed_events::TimedRowEvent;

/// A pattern is a list of events, and does not carry information about its length.
using EventList = std::vector<TimedRowEvent>;
using Pattern = EventList;

}
