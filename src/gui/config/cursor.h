#pragma once

#include "doc/timed_events.h"

namespace gui::config::cursor {

using doc::timed_events::BeatFraction;

struct MovementConfig {
    bool wrap_cursor = true;
    bool wrap_across_frames = true;
    bool arrow_follows_step = true;
    bool snap_to_events = true;

    BeatFraction page_down_distance{1};
};

}