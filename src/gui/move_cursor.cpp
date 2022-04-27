#include "move_cursor.h"
#include "doc_util/event_search.h"
#include "gui_time.h"
#include "util/compare_impl.h"
#include "util/math.h"
#include "util/release_assert.h"

#include <algorithm>

namespace gui::move_cursor {

// # Utility functions.

using chip_common::ChipIndex;
using chip_common::ChannelIndex;

/// Like gui::pattern_editor_panel::Column, but without list of subcolumns.
struct CursorChannel {
    ChipIndex chip;
    ChannelIndex channel;
};

using ChannelList = std::vector<CursorChannel>;

[[nodiscard]] static ChannelList gen_channel_list(doc::Document const & document) {
    ChannelList channel_list;

    for (
        ChipIndex chip_index = 0;
        chip_index < document.chips.size();
        chip_index++
    ) {
        for (
            ChannelIndex channel_index = 0;
            channel_index < document.chip_index_to_nchan(chip_index);
            channel_index++
        ) {
            channel_list.push_back(CursorChannel{
                .chip = chip_index,
                .channel = channel_index,
            });
        }
    }

    return channel_list;
}


// # Moving cursor by events

using doc::GridIndex;
using timing::GridAndBlock;
using timing::GridBlockBeat;

using doc::PatternRef;
using doc_util::event_search::EventSearch;

using gui_time::Wrap;
using gui_time::FwdGuiPatternIter;
using gui_time::RevGuiPatternIter;

struct MoveCursorResult {
    Wrap wrapped{};
    GridAndBeat time{};

    COMPARABLE(MoveCursorResult)
};

COMPARABLE_IMPL(MoveCursorResult, (self.wrapped, self.time))

static GridAndBeat pattern_to_abs_time(
    GridIndex grid, PatternRef pattern, doc::TimedRowEvent const& ev
) {
    return GridAndBeat{grid, pattern.begin_time + ev.anchor_beat};
}

[[nodiscard]] static MoveCursorResult prev_event_impl(
    doc::Document const& document, cursor::Cursor const cursor
) {
    auto [chip, channel] = gen_channel_list(document)[cursor.x.column];

    auto timeline = doc::TimelineChannelRef(document.timeline, chip, channel);

    auto iter = RevGuiPatternIter::from_beat(timeline, cursor.y);

    bool first = true;
    while (true) {
        auto maybe_state = iter.next();
        if (!maybe_state) {
            return MoveCursorResult{Wrap::Minus, cursor.y};
        }

        // [Wrap, GridIndex, PatternRef]
        auto [wrapped, grid, pattern] = *maybe_state;
        doc::TimedEventsRef event_list = pattern.events;
        using Rev = doc::TimedEventsRef::reverse_iterator;

        // Out-of-bounds events from a long pattern in a short block
        // are properly excluded.
        // Using a long block in a short grid cell is not excluded yet.
        if (first && wrapped == Wrap::None && grid == cursor.y.grid) {
            // We can compare timestamps directly.
            doc::BeatFraction rel_time = cursor.y.beat - pattern.begin_time;

            auto const kv = EventSearch{event_list};
            auto ev = Rev{kv.beat_begin(rel_time)};

            // Find nearest event before given time.
            if (ev != event_list.rend()) {
                return MoveCursorResult{
                    wrapped, pattern_to_abs_time(grid, pattern, *ev)
                };
            }

        } else {
            // Find last event.
            if (!event_list.empty()) {
                return MoveCursorResult{
                    wrapped, pattern_to_abs_time(grid, pattern, event_list.back())
                };
            }
        }

        first = false;
    }
}

[[nodiscard]] static MoveCursorResult next_event_impl(
    doc::Document const& document, cursor::Cursor const cursor
) {
    auto [chip, channel] = gen_channel_list(document)[cursor.x.column];

    auto timeline = doc::TimelineChannelRef(document.timeline, chip, channel);

    auto iter = FwdGuiPatternIter::from_beat(timeline, cursor.y);

    bool first = true;
    while (true) {
        auto maybe_state = iter.next();
        if (!maybe_state) {
            return MoveCursorResult{Wrap::Plus, cursor.y};
        }

        // [Wrap, GridIndex, PatternRef]
        auto [wrapped, grid, pattern] = *maybe_state;
        doc::TimedEventsRef event_list = pattern.events;

        // Out-of-bounds events from a long pattern in a short block
        // are properly excluded.
        // Using a long block in a short grid cell is not excluded yet.
        if (first && wrapped == Wrap::None && grid == cursor.y.grid) {
            // We can compare timestamps directly.
            doc::BeatFraction rel_time = cursor.y.beat - pattern.begin_time;

            auto const kv = EventSearch{event_list};
            auto ev = kv.beat_end(rel_time);

            // Find first event past given time.
            if (ev != event_list.end()) {
                return MoveCursorResult{
                    wrapped, pattern_to_abs_time(grid, pattern, *ev)
                };
            }

        } else {
            // Find first event.
            if (!event_list.empty()) {
                return MoveCursorResult{
                    wrapped, pattern_to_abs_time(grid, pattern, event_list.front())
                };
            }
        }

        first = false;
    }
}

GridAndBeat prev_event(doc::Document const& document, cursor::Cursor cursor) {
    return prev_event_impl(document, cursor).time;
}

GridAndBeat next_event(doc::Document const& document, cursor::Cursor cursor) {
    return next_event_impl(document, cursor).time;
}


// # Moving cursor by beats

using doc::GridIndex;
using doc::BeatFraction;
using doc::FractionInt;
using util::math::increment_mod;
using util::math::decrement_mod;
using util::math::frac_prev;
using util::math::frac_next;

// Move the cursor, snapping to the nearest row.

[[nodiscard]] static MoveCursorResult prev_row_impl(
    doc::Document const& document,
    GridAndBeat cursor_y,
    int rows_per_beat,
    MovementConfig const& move_cfg
) {
    BeatFraction const orig_row = cursor_y.beat * rows_per_beat;
    FractionInt const raw_prev = frac_prev(orig_row);
    FractionInt prev_row;
    auto wrapped = Wrap::None;

    if (raw_prev >= 0) {
        prev_row = raw_prev;

    } else if (move_cfg.wrap_across_frames || move_cfg.wrap_cursor) {
        if (move_cfg.wrap_across_frames) {
            if (cursor_y.grid == doc::GridIndex(0)) {
                wrapped = Wrap::Minus;
            }
            decrement_mod(
                cursor_y.grid,
                (GridIndex)document.timeline.size()
            );
        }

        auto nbeats = document.timeline[cursor_y.grid].nbeats;
        prev_row = frac_prev(nbeats * rows_per_beat);

    } else {
        // Don't move the cursor.
        return MoveCursorResult{Wrap::None, cursor_y};
    }

    cursor_y.beat = BeatFraction{prev_row, rows_per_beat};
    return MoveCursorResult{wrapped, cursor_y};
}

[[nodiscard]] static MoveCursorResult next_row_impl(
    doc::Document const& document,
    GridAndBeat cursor_y,
    int rows_per_beat,
    MovementConfig const& move_cfg
) {
    BeatFraction const orig_row = cursor_y.beat * rows_per_beat;
    FractionInt const raw_next = frac_next(orig_row);
    FractionInt next_row;
    auto wrapped = Wrap::None;

    auto nbeats = document.timeline[cursor_y.grid].nbeats;
    BeatFraction const num_rows = nbeats * rows_per_beat;

    if (raw_next < num_rows) {
        next_row = raw_next;

    } else if (move_cfg.wrap_across_frames || move_cfg.wrap_cursor) {
        if (move_cfg.wrap_across_frames) {
            increment_mod(
                cursor_y.grid,
                (GridIndex)document.timeline.size()
            );
            if (cursor_y.grid == GridIndex(0)) {
                wrapped = Wrap::Plus;
            }
        }

        next_row = 0;

    } else {
        // Don't move the cursor.
        return MoveCursorResult{Wrap::None, cursor_y};
    }

    cursor_y.beat = BeatFraction{next_row, rows_per_beat};
    return MoveCursorResult{wrapped, cursor_y};
}

GridAndBeat prev_beat(
    doc::Document const& document,
    GridAndBeat cursor_y,
    MovementConfig const& move_cfg
) {
    return prev_row_impl(document, cursor_y, 1, move_cfg).time;
}

GridAndBeat next_beat(
    doc::Document const& document,
    GridAndBeat cursor_y,
    MovementConfig const& move_cfg
) {
    return next_row_impl(document, cursor_y, 1, move_cfg).time;
}


GridAndBeat move_up(
    doc::Document const& document,
    cursor::Cursor cursor,
    MoveCursorYArgs const& args,
    MovementConfig const& move_cfg
) {
    // See doc comment in header for more docs.
    // TODO is it really necessary to call prev_row_impl() `step` times?

    // If option enabled and step > 1, move cursor by multiple rows.
    if (move_cfg.arrow_follows_step && args.step > 1) {
        for (int i = 0; i < args.step; i++) {
            cursor.y = prev_row_impl(document, cursor.y, args.rows_per_beat, move_cfg).time;
        }
        return cursor.y;
    }

    MoveCursorResult grid_row =
        prev_row_impl(document, cursor.y, args.rows_per_beat, move_cfg);

    // If option enabled and nearest event is located between cursor and nearest row,
    // move cursor to nearest event.
    if (move_cfg.snap_to_events) {
        MoveCursorResult event = prev_event_impl(document, cursor);
        return std::max(grid_row, event).time;
    }

    // Move cursor to previous row.
    return grid_row.time;
}

GridAndBeat move_down(
    doc::Document const& document,
    cursor::Cursor cursor,
    MoveCursorYArgs const& args,
    MovementConfig const& move_cfg
) {
    // See doc comment in header for more docs.

    // If option enabled and step > 1, move cursor by multiple rows.
    if (move_cfg.arrow_follows_step && args.step > 1) {
        for (int i = 0; i < args.step; i++) {
            cursor.y =
                next_row_impl(document, cursor.y, args.rows_per_beat, move_cfg).time;
        }
        return cursor.y;
    }

    MoveCursorResult grid_row =
        next_row_impl(document, cursor.y, args.rows_per_beat, move_cfg);

    // If option enabled and nearest event is located between cursor and nearest row,
    // move cursor to nearest event.
    if (move_cfg.snap_to_events) {
        MoveCursorResult event = next_event_impl(document, cursor);
        return std::min(grid_row, event).time;
    }

    // Move cursor to next row.
    return grid_row.time;
}

GridAndBeat cursor_step(
    doc::Document const& document,
    cursor::Cursor cursor,
    CursorStepArgs const& args,
    MovementConfig const& move_cfg
) {
    // See doc comment in header for more docs.

    if (args.step_to_event) {
        return next_event_impl(document, cursor).time;
    }

    if (move_cfg.snap_to_events && args.step == 1) {
        MoveCursorResult grid_row =
            next_row_impl(document, cursor.y, args.rows_per_beat, move_cfg);
        MoveCursorResult event = next_event_impl(document, cursor);

        return std::min(grid_row, event).time;
    }

    // Move cursor by multiple rows.
    for (int i = 0; i < args.step; i++) {
        cursor.y = next_row_impl(document, cursor.y, args.rows_per_beat, move_cfg).time;
    }
    return cursor.y;
}

/// To avoid an infinite loop,
/// avoid scrolling more than _ patterns in a single Page Down keystroke.
constexpr int MAX_PAGEDOWN_SCROLL = 16;

GridAndBeat page_up(
    doc::Document const& document,
    GridAndBeat cursor_y,
    MovementConfig const& move_cfg)
{
    cursor_y.beat -= move_cfg.page_down_distance;

    for (int i = 0; i < MAX_PAGEDOWN_SCROLL; i++) {
        if (cursor_y.beat < 0) {
            decrement_mod(
                cursor_y.grid, (GridIndex) document.timeline.size()
            );
            cursor_y.beat += document.timeline[cursor_y.grid].nbeats;
        } else {
            break;
        }
    }
    return cursor_y;
}

GridAndBeat page_down(
    doc::Document const& document,
    GridAndBeat cursor_y,
    MovementConfig const& move_cfg)
{
    cursor_y.beat += move_cfg.page_down_distance;

    for (int i = 0; i < MAX_PAGEDOWN_SCROLL; i++) {
        auto const & grid_cell = document.timeline[cursor_y.grid];
        if (cursor_y.beat >= grid_cell.nbeats) {
            cursor_y.beat -= grid_cell.nbeats;
            increment_mod(
                cursor_y.grid, (GridIndex) document.timeline.size()
            );
        } else {
            break;
        }
    }
    return cursor_y;
}

GridAndBeat frame_begin(
    doc::Document const& document,
    cursor::Cursor cursor,
    MovementConfig const& move_cfg)
{
    if (move_cfg.home_end_switch_patterns && cursor.y.beat <= 0) {
        if (cursor.y.grid.v > 0) {
            cursor.y.grid--;
        }
    }

    cursor.y.beat = 0;
    return cursor.y;
}

GridAndBeat frame_end(
    doc::Document const& document,
    cursor::Cursor cursor,
    MovementConfig const& move_cfg,
    doc::BeatFraction bottom_padding)
{
    // TODO pick a way of handling edge cases.
    //  We should use the same method of moving the cursor to end of pattern,
    //  as switching patterns uses (switch_grid_index()).
    //  calc_bottom() is dependent on selection's cached rows_per_beat (limitation)
    //  but selects one pattern exactly (good).

    auto calc_bottom = [&] (GridAndBeat cursor_y) -> BeatFraction {
        return document.timeline[cursor_y.grid].nbeats - bottom_padding;
    };

    auto bottom_beat = calc_bottom(cursor.y);

    if (move_cfg.home_end_switch_patterns && cursor.y.beat >= bottom_beat) {
        if (cursor.y.grid + 1 < document.timeline.size()) {
            cursor.y.grid++;
            bottom_beat = calc_bottom(cursor.y);
        }
    }

    cursor.y.beat = bottom_beat;

    return cursor.y;
}

GridAndBeat prev_frame(
    doc::Document const& document,
    cursor::Cursor cursor,
    int zoom_level)
{
    decrement_mod(cursor.y.grid, (GridIndex) document.timeline.size());

    BeatFraction nbeats = document.timeline[cursor.y.grid].nbeats;

    // If cursor is out of bounds, move to last row in frame.
    if (cursor.y.beat >= nbeats) {
        BeatFraction row_count = nbeats * zoom_level;
        int last_row = util::math::frac_prev(row_count);
        cursor.y.beat = BeatFraction{last_row, zoom_level};
    }

    return cursor.y;
}

GridAndBeat next_frame(
    doc::Document const& document,
    cursor::Cursor cursor,
    int zoom_level)
{
    increment_mod(cursor.y.grid, (GridIndex) document.timeline.size());

    BeatFraction nbeats = document.timeline[cursor.y.grid].nbeats;

    // If cursor is out of bounds, move to last row in frame.
    if (cursor.y.beat >= nbeats) {
        BeatFraction row_count = nbeats * zoom_level;
        int last_row = util::math::frac_prev(row_count);
        cursor.y.beat = BeatFraction{last_row, zoom_level};
    }

    return cursor.y;
}

} // namespace

#ifdef UNITTEST

#include <doctest.h>
#include "test_utils/parameterize.h"

#include "timing_common.h"
#include "doc.h"
#include "chip_kinds.h"
#include "doc_util/event_builder.h"
#include "doc_util/sample_instrs.h"

namespace gui::move_cursor {

using namespace doc;
using chip_kinds::ChipKind;
using namespace doc_util::event_builder;
using doc_util::sample_instrs::spc_chip_channel_settings;

static Document empty_doc(int n_seq_entry) {
    SequencerOptions sequencer_options{
        .target_tempo = 100,
    };

    Timeline timeline;

    for (int seq_entry_idx = 0; seq_entry_idx < n_seq_entry; seq_entry_idx++) {
        timeline.push_back(TimelineFrame {
            .nbeats = 4,
            .chip_channel_cells = {
                // chip 0
                {
                    // 8 channels
                    {}, {}, {}, {}, {}, {}, {}, {}
                }
            },
        });
    }

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = Samples(),
        .instruments = Instruments(),
        .chips = {ChipKind::Spc700},
        .chip_channel_settings = spc_chip_channel_settings(),
        .timeline = timeline
    };
}

// # Documents for event search

using timing::GridAndBeat;
using cursor::Cursor;

/// Uses a single full-grid block to store events.
static doc::Document simple_document() {
    auto document = empty_doc(4);
    auto & cell = document.timeline[1].chip_channel_cells[0][0];

    auto block = doc::TimelineBlock{0, END_OF_GRID, Pattern{}};

    block.pattern.events.push_back({1, {1}});
    block.pattern.events.push_back({2, {2}});

    cell._raw_blocks = {std::move(block)};

    return document;
}

/// Uses two mid-grid blocks to store events.
static doc::Document blocked_document() {
    auto document = empty_doc(4);
    auto & cell = document.timeline[1].chip_channel_cells[0][0];

    auto block1 = doc::TimelineBlock{1, 2, Pattern{
        .events = {{0, {1}}}
    }};

    auto block2 = doc::TimelineBlock{2, 3, Pattern{
        .events = {{0, {2}}}
    }};

    cell._raw_blocks = {std::move(block1), std::move(block2)};

    return document;
}

/// Uses a looped pattern to play the same event multiple times.
static doc::Document looped_document() {
    auto document = empty_doc(4);
    auto & cell = document.timeline[1].chip_channel_cells[0][0];

    auto block = doc::TimelineBlock{1, 3, Pattern{
        .events = {{0, {1}}},
        .loop_length = 1,
    }};

    cell._raw_blocks = {std::move(block)};

    return document;
}

PARAMETERIZE(event_search_doc, doc::Document, document,
    OPTION(document, simple_document());
    OPTION(document, blocked_document());
    OPTION(document, looped_document());
)


TEST_CASE("Test next_event_impl()/prev_event_impl()") {
    cursor::CursorX const x{0, 0};

    enum WhichFunction {
        PrevEvent,
        NextEvent,
    };

    struct TestCase {
        GridAndBeat start;
        WhichFunction which_function;
        GridAndBeat end;
        Wrap wrapped;
        char const * message;
    };

    TestCase test_cases[] {
        // next_event_impl()
        {{0, 0}, NextEvent, {1, 1}, Wrap::None, "Ensure next_event_impl() moves to next pattern if current empty"},
        {{1, 0}, NextEvent, {1, 1}, Wrap::None, "Ensure next_event_impl() works"},
        {{1, 1}, NextEvent, {1, 2}, Wrap::None, "Ensure next_event_impl() skips current event"},
        {{1, 2}, NextEvent, {1, 1}, Wrap::Plus, "Ensure next_event_impl() skips current event and wraps to same pattern"},
        {{1, 3}, NextEvent, {1, 1}, Wrap::Plus, "Ensure next_event_impl() wraps to same pattern"},
        {{2, 0}, NextEvent, {1, 1}, Wrap::Plus, "Ensure next_event_impl() wraps"},
        // prev_event_impl()
        {{2, 0}, PrevEvent, {1, 2}, Wrap::None, "Ensure prev_event_impl() moves to next pattern if current empty"},
        {{1, 3}, PrevEvent, {1, 2}, Wrap::None, "Ensure prev_event_impl() works"},
        {{1, 2}, PrevEvent, {1, 1}, Wrap::None, "Ensure prev_event_impl() skips current event"},
        {{1, 1}, PrevEvent, {1, 2}, Wrap::Minus, "Ensure prev_event_impl() skips current event and wraps to same pattern"},
        {{1, 0}, PrevEvent, {1, 2}, Wrap::Minus, "Ensure prev_event_impl() wraps to same pattern"},
        {{0, 0}, PrevEvent, {1, 2}, Wrap::Minus, "Ensure prev_event_impl() wraps"},
    };

    doc::Document document{doc::DocumentCopy{}};
    PICK(event_search_doc(document));

    for (TestCase test_case : test_cases) {
        CAPTURE(test_case.message);

        MoveCursorResult out;
        if (test_case.which_function == NextEvent) {
            out = next_event_impl(document, Cursor{x, test_case.start});
        } else {
            out = prev_event_impl(document, Cursor{x, test_case.start});
        }

        CHECK(out.time == test_case.end);
        CHECK(out.wrapped == test_case.wrapped);
    }
}

TEST_CASE("Test next_event_impl()/prev_event_impl() on empty document") {
    auto document = empty_doc(4);

    cursor::CursorX const x{0, 0};

    // Both functions should be no-op identity functions on empty documents,
    // and indicate that the cursor has wrapped.
    {
        MoveCursorResult out = next_event_impl(document, Cursor{x, {1, 2}});
        CHECK(out.time == GridAndBeat{1, 2});
        CHECK(out.wrapped == Wrap::Plus);
    }
    {
        MoveCursorResult out = prev_event_impl(document, Cursor{x, {1, 2}});
        CHECK(out.time == GridAndBeat{1, 2});
        CHECK(out.wrapped == Wrap::Minus);
    }
}

static BeatFraction time(int start, int num, int den) {
    return start + BeatFraction(num, den);
}

TEST_CASE("Test prev_row_impl()/next_row_impl()") {
    auto document = empty_doc(4);
    int rows_per_beat = 4;

    MovementConfig move_cfg{
        .wrap_cursor = true,
        .wrap_across_frames = true,
    };

    enum WhichFunction {
        PrevRow,
        NextRow,
    };

    struct TestCase {
        WhichFunction which_function;
        GridAndBeat start;
        GridAndBeat end;
        char const * message;
    };

    TestCase test_cases[] {
        // next_row_impl()
        {NextRow, {0, {0, 4}}, {0, {1, 4}}, "next_row_impl() 0, 0/4"},
        {NextRow, {0, {1, 8}}, {0, {1, 4}}, "next_row_impl() 0, 1/8"},
        {NextRow, {0, {1, 4}}, {0, {2, 4}}, "next_row_impl() 0, 1/4"},
        // prev_row_impl()
        {PrevRow, {0, {2, 4}}, {0, {1, 4}}, "prev_row_impl() 0, 2/4"},
        {PrevRow, {0, {3, 8}}, {0, {1, 4}}, "prev_row_impl() 0, 3/8"},
        {PrevRow, {0, {1, 4}}, {0, {0, 4}}, "prev_row_impl() 0, 1/4"},
    };

    for (TestCase test_case : test_cases) {
        CAPTURE(test_case.message);

        MoveCursorResult out;
        if (test_case.which_function == NextRow) {
            out = next_row_impl(document, test_case.start, rows_per_beat, move_cfg);
        } else {
            out = prev_row_impl(document, test_case.start, rows_per_beat, move_cfg);
        }

        CHECK(out == MoveCursorResult{Wrap::None, test_case.end});
    }
}

TEST_CASE("Test prev_row_impl()/next_row_impl() wrapping") {
    // nbeats=4, npattern=4
    auto document = empty_doc(4);
    int rows_per_beat = 4;

    MovementConfig wrap_doc_1{
        .wrap_cursor = true,
        .wrap_across_frames = true,
    };

    // In 0CC, if "wrap across frames" is true,
    // vertical wrapping always happens and "wrap cursor" is ignored.
    MovementConfig wrap_doc_2{
        .wrap_cursor = false,
        .wrap_across_frames = true,
    };

    MovementConfig wrap_pattern{
        .wrap_cursor = true,
        .wrap_across_frames = false,
    };

    MovementConfig no_wrap{
        .wrap_cursor = false,
        .wrap_across_frames = false,
    };

    enum WhichFunction {
        PrevRow,
        NextRow,
    };

    /*
    how many separate toggles/modes should I have for
    "enable end-of-document wrap" and "enable moving between patterns" and
    "wrap around top/bottom of a single pattern"?
    */
    struct TestCase {
        WhichFunction which_function;
        GridAndBeat start;
        MoveCursorResult wrap_doc;
        GridAndBeat wrap_pattern;
        // If wrap is turned off, the position should be unchanged (= start).
        char const * message;
    };

    BeatFraction end = time(3, 3, 4);
    TestCase test_cases[] {
        {NextRow, {0, end}, {Wrap::None, {1, 0}}, {0, 0}, "Ensure that next_row_impl() wraps properly"},
        {NextRow, {0, 100}, {Wrap::None, {1, 0}}, {0, 0}, "Ensure that next_row_impl() wraps on out-of-bounds"},
        {NextRow, {3, end}, {Wrap::Plus, {0, 0}}, {3, 0}, "Ensure that next_row_impl() wraps around the document"},
        {PrevRow, {1, 0}, {Wrap::None, {0, end}}, {1, end}, "Ensure that prev_row_impl() wraps properly"},
        {PrevRow, {1, -1}, {Wrap::None, {0, end}}, {1, end}, "Ensure that prev_row_impl() wraps on out-of-bounds"},
        {PrevRow, {0, 0}, {Wrap::Minus, {3, end}}, {0, end}, "Ensure that prev_row_impl() wraps around the document"},
    };

    for (TestCase test_case : test_cases) {
        CAPTURE(test_case.message);

        auto verify = [&] (
            char const* move_name, MovementConfig const& move_cfg, MoveCursorResult end
        ) {
            CAPTURE(move_name);
            MoveCursorResult out;
            if (test_case.which_function == NextRow) {
                out = next_row_impl(document, test_case.start, rows_per_beat, move_cfg);
            } else {
                out = prev_row_impl(document, test_case.start, rows_per_beat, move_cfg);
            }

            CHECK(out == end);
        };

        verify("wrap_doc_1", wrap_doc_1, test_case.wrap_doc);
        verify("wrap_doc_2", wrap_doc_2, test_case.wrap_doc);
        verify("wrap_pattern", wrap_pattern, MoveCursorResult{Wrap::None, test_case.wrap_pattern});
        verify("no_wrap", no_wrap, MoveCursorResult{Wrap::None, test_case.start});
    }
}

// TODO add tests for cursor_step

//TEST_CASE("Test move_up()/move_down() wrapping and events") {
//    // nbeats=4, npattern=4
//    auto document = empty_doc(4);
//    MoveCursorYArgs args{.rows_per_beat = 4, .step = 1};

//    MovementConfig move_cfg{
//        .wrap_cursor = true,
//        .wrap_across_frames = true,
//    };

//    BeatFraction row_0 = 0;
//    BeatFraction mid_row_0 = time(0, 1, 8);
//    BeatFraction row_1 = time(0, 1, 4);

//    BeatFraction last_row = time(3, 3, 4);
//    BeatFraction mid_last_row = time(3, 7, 8);


//    // Add event midway through pattern 1, row 0.
//    document.sequence[1].chip_channel_events[0][0].push_back(
//        TimedRowEvent{TimeInPattern{time(0, 1, 8), 0}, RowEvent{0}}
//    );
//    // Add event midway through pattern 1, final row.
//    document.sequence[1].chip_channel_events[0][0].push_back(
//        TimedRowEvent{TimeInPattern{mid_last_row, 0}, RowEvent{0}}
//    );
//    // Add event midway through pattern 3, final row of document.
//    document.sequence[3].chip_channel_events[0][0].push_back(
//        TimedRowEvent{TimeInPattern{mid_last_row, 0}, RowEvent{0}}
//    );

//    auto event_in_middle = empty_doc(4);
//    event_in_middle.sequence[2].chip_channel_events[0][0].push_back(
//        TimedRowEvent{TimeInPattern{2, 0}, RowEvent{0}}
//    );

//    enum WhichFunction {
//        MoveUp,
//        MoveDown,
//    };

//    /*
//    how many separate toggles/modes should I have for
//    "enable end-of-document wrap" and "enable moving between patterns" and
//    "wrap around top/bottom of a single pattern"?
//    */
//    struct TestCase {
//        WhichFunction which_function;
//        GridAndBeat start;
//        GridAndBeat end;
//        // If wrap is turned off, the position should be unchanged (= start).
//        char const * message;
//    };

//    TestCase test_cases[] {
//        {MoveDown, {0, row_0}, {0, row_1},
//            "Ensure that move_down() works normally if no events present"},
//        {MoveDown, {1, row_0}, {1, mid_row_0},
//            "Ensure that move_down() locks onto mid-row events"},
//        {MoveDown, {1, last_row}, {1, mid_last_row},
//            "Ensure that move_down() locks onto mid-row events at the end of a pattern"},
//        {MoveDown, {3, last_row}, {3, mid_last_row},
//            "Ensure that move_down() locks onto mid-row events at the end of the document"},
//        {MoveUp, {0, row_1}, {0, row_0},
//            "Ensure that move_up() works normally if no events present"},
//        {MoveUp, {1, row_1}, {1, mid_row_0},
//            "Ensure that move_up() locks onto mid-row events"},
//        {MoveUp, {2, row_0}, {1, mid_last_row},
//            "Ensure that move_up() locks onto mid-row events at the end of a pattern"},
//        {MoveUp, {0, row_0}, {3, mid_last_row},
//            "Ensure that move_up() locks onto mid-row events at the end of the document"},
//    };

//    for (TestCase test_case : test_cases) {
//        CAPTURE(test_case.message);

//        Cursor cursor{{0, 0}, test_case.start};

//        GridAndBeat out;
//        if (test_case.which_function == MoveDown) {
//            out = move_down(document, cursor, args, move_cfg);
//        } else {
//            out = move_up(document, cursor, args, move_cfg);
//        }

//        CHECK(out == test_case.end);
//    }

//    // Ensure that move_down() doesn't lock onto distant events
//    {
//        Cursor cursor{{0, 0}, {3, last_row}};
//        CHECK(
//            move_down(event_in_middle, cursor, args, move_cfg)
//            == GridAndBeat{0, row_0}
//        );
//    }

//    // Ensure that move_up() doesn't lock onto distant events
//    {
//        Cursor cursor{{0, 0}, {0, 0}};
//        CHECK(
//            move_up(event_in_middle, cursor, args, move_cfg)
//            == GridAndBeat{3, last_row}
//        );
//    }
//}

}

#endif
