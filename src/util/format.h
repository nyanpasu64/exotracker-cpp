#pragma once

#include "doc/timed_events.h"

#include <string>

[[nodiscard]] std::string format_frac(doc::timed_events::BeatFraction frac);
