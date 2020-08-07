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

/// Return the timestamp of the nearest event
/// whose beat fraction is strictly less than the cursor's.
///
/// May wrap around to the beginning of the song.
///
/// If the channel has no events, returns {original time, wrapped=true}.
[[nodiscard]]
PatternAndBeat prev_event(doc::Document const& document, cursor::Cursor cursor);

/// Return the timestamp of the next event
/// whose beat fraction is strictly greater than the cursor's.
///
/// May wrap around to the beginning of the song.
///
/// If the channel has no events, returns {original time, wrapped=true}.
[[nodiscard]]
PatternAndBeat next_event(doc::Document const& document, cursor::Cursor cursor);


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

/// Options owned by pattern editor panel and set in GUI, not set in settings dialog.
struct MoveCursorYArgs {
    int rows_per_beat;
    int step;
};

/// - If step > 1 and we follow step, move to nearest row above `step` times.
/// - If sub-row movement is enabled,
///   and prev_event() doesn't wrap and is closer than the nearest row above,
///   jump up to prev_event().time.
/// - Move to nearest row above.
[[nodiscard]] PatternAndBeat move_up(
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
[[nodiscard]] PatternAndBeat move_down(
    doc::Document const& document,
    cursor::Cursor cursor,
    MoveCursorYArgs const& args,
    MovementConfig const& move_cfg
);


/// - If step = 1, sub-row movement is enabled,
///   and next_event() doesn't wrap and is closer than the nearest row below,
///   jump down to next_event().time.
/// - Move to nearest row below `step` times.
[[nodiscard]] PatternAndBeat cursor_step(
    doc::Document const& document,
    cursor::Cursor cursor,
    MoveCursorYArgs const& args,
    MovementConfig const& move_cfg
);


// TODO is it really necessary to call next_row() `step` times?

}
