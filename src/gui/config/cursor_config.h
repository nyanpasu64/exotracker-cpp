#pragma once

#include "doc/timed_events.h"

namespace gui::config::cursor_config {

using doc::timed_events::BeatFraction;

struct MovementConfig {
    bool wrap_cursor = true;
    bool wrap_across_frames = true;
    bool home_end_switch_patterns = true;
    bool arrow_follows_step = true;
    bool snap_to_events = false;

    BeatFraction page_down_distance{1};
};

}
