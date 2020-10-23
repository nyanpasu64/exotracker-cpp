#pragma once

/// Only vertical (in time) cursor movement is implemented here.
/// Horizontal (columns/subcolumns/digits) movement is still in
/// pattern_editor_panel.cpp.

#include "cursor.h"
#include "doc.h"
#include "gui/config/cursor_config.h"
#include "timing_common.h"
#include "chip_common.h"

#include <vector>

namespace gui::move_cursor {

using timing::GridAndBeat;


// # Moving cursor by events

/// Return the timestamp of the nearest event
/// whose beat fraction is strictly less than the cursor's.
///
/// May wrap around to the beginning of the song.
///
/// If the channel has no events, returns {original time, wrapped=true}.
[[nodiscard]]
GridAndBeat prev_event(doc::Document const& document, cursor::Cursor cursor);

/// Return the timestamp of the next event
/// whose beat fraction is strictly greater than the cursor's.
///
/// May wrap around to the beginning of the song.
///
/// If the channel has no events, returns {original time, wrapped=true}.
[[nodiscard]]
GridAndBeat next_event(doc::Document const& document, cursor::Cursor cursor);


// # Moving cursor by beats

using config::cursor_config::MovementConfig;

[[nodiscard]] GridAndBeat prev_beat(
    doc::Document const& document,
    GridAndBeat cursor_y,
    MovementConfig const& move_cfg
);

[[nodiscard]] GridAndBeat next_beat(
    doc::Document const& document,
    GridAndBeat cursor_y,
    MovementConfig const& move_cfg
);

/// Options owned by pattern editor panel and set in GUI, not set in settings dialog.
struct MoveCursorYArgs {
    int rows_per_beat;
    int step;
    bool step_to_event;
};

/// - If step > 1 and we follow step, move to nearest row above `step` times.
/// - If sub-row movement is enabled,
///   and prev_event() doesn't wrap and is closer than the nearest row above,
///   jump up to prev_event().time.
/// - Move to nearest row above.
[[nodiscard]] GridAndBeat move_up(
    doc::Document const& document,
    cursor::Cursor cursor,
    MoveCursorYArgs const& args,
    MovementConfig const& move_cfg
);

/// - If step > 1 and we follow step, move to nearest row below `step` times.
/// - If sub-row movement is enabled,
///   and next_event() doesn't wrap and is closer than the nearest row below,
///   jump down to next_event().time.
/// - Move to nearest row below.
[[nodiscard]] GridAndBeat move_down(
    doc::Document const& document,
    cursor::Cursor cursor,
    MoveCursorYArgs const& args,
    MovementConfig const& move_cfg
);


/// - If "step to event" enabled, move to the next event.
/// - If step = 1, sub-row movement is enabled,
///   and next_event() doesn't wrap and is closer than the nearest row below,
///   jump down to next_event().time.
/// - Move to nearest row below `step` times.
[[nodiscard]] GridAndBeat cursor_step(
    doc::Document const& document,
    cursor::Cursor cursor,
    MoveCursorYArgs const& args,
    MovementConfig const& move_cfg
);

}
