#pragma once

#include "doc/timed_events.h"

namespace gui::config::cursor_config {

using doc::timed_events::TickT;

struct MovementConfig {
    bool home_end_switch_patterns = true;
    bool arrow_follows_step = true;
    bool snap_to_events = false;

    int page_down_rows = 4;
};

}
