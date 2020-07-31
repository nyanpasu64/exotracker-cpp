#pragma once

#include "cursor.h"
#include "doc.h"
#include "gui/config/cursor.h"
#include "timing_common.h"
#include "chip_common.h"
#include "util/compare.h"

#include <vector>

namespace gui::move_cursor {

using timing::PatternAndBeat;


// # Moving cursor by events

struct SwitchEventResult {
    PatternAndBeat time;
    bool wrapped;
};

/// Return the timestamp of the nearest event
/// whose beat fraction is strictly less than the cursor's.
///
/// May wrap around to the beginning of the song.
///
/// If the channel has no events, returns {original time, wrapped=true}.
[[nodiscard]]
SwitchEventResult prev_event(doc::Document const& document, cursor::Cursor cursor);

/// Return the timestamp of the next event
/// whose beat fraction is strictly greater than the cursor's.
///
/// May wrap around to the beginning of the song.
///
/// If the channel has no events, returns {original time, wrapped=true}.
[[nodiscard]]
SwitchEventResult next_event(doc::Document const& document, cursor::Cursor cursor);


// # Moving cursor by beats

using config::cursor::MovementConfig;

[[nodiscard]] PatternAndBeat prev_beat(
    doc::Document const& document,
    PatternAndBeat cursor_y,
    MovementConfig const& move_cfg
);

[[nodiscard]] PatternAndBeat next_beat(
    doc::Document const& document,
    PatternAndBeat cursor_y,
    MovementConfig const& move_cfg
);

}
