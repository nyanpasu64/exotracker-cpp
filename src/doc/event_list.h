#pragma once

#include "events.h"
#include "timed_events.h"

#include <vector>

namespace doc::event_list {

using events::RowEvent;
using timed_events::TimeInPattern;
using timed_events::TimedRowEvent;

using EventIndex = uint32_t;

using EventList = std::vector<TimedRowEvent>;

}
