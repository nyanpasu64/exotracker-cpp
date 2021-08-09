#pragma once

#include "events.h"
#include "timed_events.h"

#include <gsl/span>

#include <vector>

namespace doc::event_list {

using events::RowEvent;
using timed_events::TimeInPattern;
using timed_events::TimedRowEvent;

using EventIndex = uint32_t;

/// The maximum number of events allowed in a single pattern during file loading,
/// or at any point during editing.
///
/// 256 events is insufficient for very long patterns (like if you wanted to store an
/// entire classical movement in a single pattern without breaks).
///
/// 65535 events is more than sufficient to store even pathological monophonic tracks.
///
/// EventIndex < EventList.size() <= MAX_EVENTS_PER_PATTERN.
constexpr int MAX_EVENTS_PER_PATTERN = 0xffff;

using EventList = std::vector<TimedRowEvent>;
using TimedEventsRef = gsl::span<timed_events::TimedRowEvent const>;

}
