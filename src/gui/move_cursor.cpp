#include "move_cursor.h"
#include "doc_util/event_search.h"
#include "doc_util/time_util.h"
#include "doc_util/track_util.h"
#include "gui_time.h"
#include "util/compare_impl.h"
#include "util/expr.h"
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

using doc::PatternRef;
using doc_util::event_search::EventSearch;

using gui_time::FwdGuiPatternIter;
using gui_time::RevGuiPatternIter;

static TickT pattern_to_abs_time(PatternRef pattern, doc::TimedRowEvent const& ev) {
    return pattern.begin_tick + ev.anchor_tick;
}


[[nodiscard]] static TickT prev_event_impl(
    doc::Document const& document, cursor::Cursor const cursor
) {
    const auto [chip, channel] = gen_channel_list(document)[cursor.x.column];

    doc::SequenceTrackRef track = document.sequence[chip][channel];

    // Loop over all patterns backwards, starting from the cursor's current pattern.
    auto patterns = RevGuiPatternIter::from_time(track, cursor.y);
    bool contains_cursor = true;
    while (true) {
        auto maybe_pattern = patterns.next();
        if (!maybe_pattern) {
            return cursor.y;
        }

        PatternRef pattern = *maybe_pattern;
        doc::TimedEventsRef event_list = pattern.events;
        using Rev = doc::TimedEventsRef::reverse_iterator;

        if (contains_cursor) {
            // Pattern holds cursor.
            const TickT rel_time = cursor.y - pattern.begin_tick;
            const auto kv = EventSearch(event_list);

            // Find nearest event before cursor time.
            auto ev = Rev(kv.tick_begin(rel_time));
            if (ev != event_list.rend()) {
                return pattern_to_abs_time(pattern, *ev);
            }

        } else {
            // Pattern lies before cursor; find last event.
            if (!event_list.empty()) {
                return pattern_to_abs_time(pattern, event_list.back());
            }
        }

        contains_cursor = false;
    }
}

[[nodiscard]] static TickT next_event_impl(
    doc::Document const& document, cursor::Cursor const cursor
) {
    const auto [chip, channel] = gen_channel_list(document)[cursor.x.column];

    doc::SequenceTrackRef track = document.sequence[chip][channel];

    // Loop over all patterns forwards, starting from the cursor's current pattern.
    auto patterns = FwdGuiPatternIter::from_time(track, cursor.y);
    bool contains_cursor = true;
    while (true) {
        auto maybe_pattern = patterns.next();
        if (!maybe_pattern) {
            return cursor.y;
        }

        auto pattern = *maybe_pattern;
        doc::TimedEventsRef event_list = pattern.events;

        if (contains_cursor) {
            // Pattern holds cursor.
            const TickT rel_time = cursor.y - pattern.begin_tick;
            const auto kv = EventSearch(event_list);

            // Find nearest event after cursor time.
            auto ev = kv.tick_end(rel_time);
            if (ev != event_list.end()) {
                return pattern_to_abs_time(pattern, *ev);
            }

        } else {
            // Pattern lies after cursor; find first event.
            if (!event_list.empty()) {
                return pattern_to_abs_time(pattern, event_list.front());
            }
        }

        contains_cursor = false;
    }
}

TickT prev_event(doc::Document const& document, cursor::Cursor cursor) {
    return prev_event_impl(document, cursor);
}

TickT next_event(doc::Document const& document, cursor::Cursor cursor) {
    return next_event_impl(document, cursor);
}


// # Moving cursor by beats

using doc_util::time_util::BeatIter;
using doc_util::time_util::RowIter;

// Move the cursor, snapping to the nearest row.

TickT prev_beat(doc::Document const& doc, TickT cursor_y) {
    auto x = BeatIter::at_time(doc, cursor_y);

    // If cursor starts between beats, BeatIter already snaps to previous beat, so skip
    // calling prev.
    if (!x.snapped_earlier) {
        x.iter.try_prev();
    }
    return x.iter.peek().time;
}

TickT next_beat(doc::Document const& doc, TickT cursor_y) {
    auto x = BeatIter::at_time(doc, cursor_y);
    x.iter.next();
    return x.iter.peek().time;
}

static TickT prev_rows(
    doc::Document const& doc, TickT cursor_y, int ticks_per_row, int step
) {
    auto x = RowIter::at_time(doc, cursor_y, ticks_per_row);

    // This code is wrong for step=0, but this function never gets called with that.
    assert(step >= 1);

    // If cursor starts between rows (!row_at_cursor), RowIter already snaps to
    // previous beat, so skip the first call to prev.
    ptrdiff_t i = x.snapped_earlier ? 1 : 0;
    for (; i < step; i++) {
        x.iter.try_prev();
    }
    return x.iter.peek().time;
}

static TickT next_rows(
    doc::Document const& doc, TickT cursor_y, int ticks_per_row, int step
) {
    auto x = RowIter::at_time(doc, cursor_y, ticks_per_row);
    for (ptrdiff_t i = 0; i < step; i++) {
        x.iter.next();
    }
    return x.iter.peek().time;
}

TickT move_up(
    doc::Document const& document,
    cursor::Cursor cursor,
    MoveCursorYArgs const& args,
    MovementConfig const& move_cfg
) {
    // See doc comment in header for more docs.
    // TODO is it really necessary to call prev_row_impl() `step` times?

    // If option enabled and step > 1, move cursor by multiple rows.
    if (move_cfg.arrow_follows_step && args.step > 1) {
        cursor.y = prev_rows(document, cursor.y, args.ticks_per_row, args.step);
        return cursor.y;
    }

    TickT grid_row = prev_rows(document, cursor.y, args.ticks_per_row, 1);

    // If option enabled and nearest event is located between cursor and nearest row,
    // move cursor to nearest event.
    if (move_cfg.snap_to_events) {
        TickT event = prev_event_impl(document, cursor);
        return std::max(grid_row, event);
    }

    // Move cursor to previous row.
    return grid_row;
}

TickT move_down(
    doc::Document const& document,
    cursor::Cursor cursor,
    MoveCursorYArgs const& args,
    MovementConfig const& move_cfg
) {
    // See doc comment in header for more docs.

    // If option enabled and step > 1, move cursor by multiple rows.
    if (move_cfg.arrow_follows_step && args.step > 1) {
        cursor.y = next_rows(document, cursor.y, args.ticks_per_row, args.step);
        return cursor.y;
    }

    TickT grid_row = next_rows(document, cursor.y, args.ticks_per_row, 1);

    // If option enabled and nearest event is located between cursor and nearest row,
    // move cursor to nearest event.
    if (move_cfg.snap_to_events) {
        TickT event = next_event_impl(document, cursor);
        return std::min(grid_row, event);
    }

    // Move cursor to next row.
    return grid_row;
}

TickT cursor_step(
    doc::Document const& document,
    cursor::Cursor cursor,
    CursorStepArgs const& args,
    MovementConfig const& move_cfg
) {
    // See doc comment in header for more docs.

    if (args.step_to_event) {
        return next_event_impl(document, cursor);
    }

    if (move_cfg.snap_to_events && args.step == 1) {
        TickT grid_row =
            next_rows(document, cursor.y, args.ticks_per_row, 1);
        TickT event = next_event_impl(document, cursor);

        return std::min(grid_row, event);
    }

    // Move cursor by multiple rows.
    cursor.y = next_rows(document, cursor.y, args.ticks_per_row, args.step);
    return cursor.y;
}

TickT page_up(
    doc::Document const& document,
    TickT cursor_y,
    int ticks_per_row,
    MovementConfig const& move_cfg)
{
    const TickT page_up_distance = ticks_per_row * move_cfg.page_down_rows;

    if (cursor_y >= page_up_distance) {
        cursor_y -= page_up_distance;
    } else {
        cursor_y = 0;
    }

    return cursor_y;
}

TickT page_down(
    doc::Document const& document,
    TickT cursor_y,
    int ticks_per_row,
    MovementConfig const& move_cfg)
{
    const TickT page_down_distance = ticks_per_row * move_cfg.page_down_rows;

    cursor_y += page_down_distance;

    return cursor_y;
}

TickT block_begin(
    doc::Document const& document,
    cursor::Cursor cursor,
    MovementConfig const& move_cfg)
{
    // TODO snap to top of current block, and when pressed again snap to previous block
    /*
    fn calc_top(PatternRef) = PatternRef.begin_tick
    iter = RevTrackPatternWrap(cursor) [begin is inclusive]
    block = iter.next()
    release_assert block and not wrapped
    if (move_cfg.home_end_switch_patterns && cursor.y <= top_tick) {
        block = iter.next()
        if block && block.wrapped == Wrap::None {
            cursor.y = calc_top(block.pattern);
        } else {
            // leave cursor unchanged? goto begin of song?
        }
    } else {
        cursor.y = calc_top(block.pattern);
    }
    */

    cursor.y = 0;

    return cursor.y;
}

TickT block_end(
    doc::Document const& document,
    cursor::Cursor cursor,
    MovementConfig const& move_cfg,
    TickT bottom_padding)
{
    // TODO pick a way of handling edge cases.
    //  We should use the same method of moving the cursor to end of pattern,
    //  as switching patterns uses (switch_grid_index()).
    //  calc_bottom() is dependent on selection's cached rows_per_beat (limitation)
    //  but selects one pattern exactly (good).

    /*
    fn calc_bottom(PatternRef) = max(PatternRef.end_tick - bottom_padding, PatternRef.begin_tick)
    iter = FwdTrackPatternWrap(cursor) [begin is inclusive]
    block = iter.next()
    release_assert block and not wrapped
    if (move_cfg.home_end_switch_patterns && cursor.y >= bottom_tick) {
        block = iter.next()
        if block && block.wrapped == Wrap::None {
            cursor.y = calc_bottom(block.pattern);
        } else {
            // leave cursor unchanged? goto max(end of song - padding, now)?
        }
    } else {
        cursor.y = calc_bottom(block.pattern);
    }
    */

    using doc_util::track_util::song_length;

    return song_length(document.sequence);
}

TickT prev_block(
    doc::Document const& document,
    cursor::Cursor cursor,
    int zoom_level)
{
    // TODO implement RevGuiTrackIter
    return cursor.y;
}

TickT next_block(
    doc::Document const& document,
    cursor::Cursor cursor,
    int zoom_level)
{
    // TODO implement FwdGuiTrackIter
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

static Document empty_doc() {
    SequencerOptions sequencer_options{
        .target_tempo = 100,
    };

    Sequence sequence = {{{}, {}, {}, {}, {}, {}, {}, {}}};

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = Samples(),
        .instruments = Instruments(),
        .chips = {ChipKind::Spc700},
        .sequence = sequence
    };
}

// # Documents for event search

using timing::TickT;
using cursor::Cursor;

// Create three documents with events at at(1, 1) and at(2, 1) (tick 49 and 97).

/// Uses a single block to store events.
static Document simple_document() {
    auto document = empty_doc();
    auto & track = document.sequence[0][0];

    auto block = TrackBlock::from_events(0, at(4), {
        {at(1, 1), {1}},
        {at(2, 1), {2}},
    });

    track.blocks = {std::move(block)};

    return document;
}

/// Uses two blocks to store events.
static Document blocked_document() {
    auto document = empty_doc();
    auto & track = document.sequence[0][0];

    auto block1 = TrackBlock::from_events(at(1), at(1), {
        {at(0, 1), {1}}
    });

    auto block2 = TrackBlock::from_events(at(2), at(1), {
        {at(0, 1), {2}}
    });

    track.blocks = {std::move(block1), std::move(block2)};

    return document;
}

/// Uses a looped pattern to play the same event multiple times.
static Document looped_document() {
    auto document = empty_doc();
    auto & track = document.sequence[0][0];

    auto block = TrackBlock::from_events(at(1), at(1), {
        {at(0, 1), {1}}
    }, 2);

    track.blocks = {std::move(block)};

    return document;
}

PARAMETERIZE(event_search_doc, Document, document,
    OPTION(document, simple_document());
    OPTION(document, blocked_document());
    OPTION(document, looped_document());
)


TEST_CASE("Test next_event_impl()/prev_event_impl()") {
    cursor::CursorX const x{0, 0};

    // Pick one of 3 documents with events at time 49 and 97.
    Document document{DocumentCopy{}};
    PICK(event_search_doc(document));

    enum WhichFunction {
        PrevEvent,
        NextEvent,
    };

    struct TestCase {
        TickT start;
        WhichFunction which_function;
        TickT end;
        char const * message;
    };

    // Test locating events from various times.
    TestCase test_cases[] {
        // next_event_impl()
        {at(0, 0), NextEvent, at(1, 1), "Ensure next_event_impl() moves to next pattern if current empty"},
        {at(1, 0), NextEvent, at(1, 1), "Ensure next_event_impl() works"},
        {at(1, 1), NextEvent, at(2, 1), "Ensure next_event_impl() skips current event"},
        {at(2, 1), NextEvent, at(2, 1), "Ensure next_event_impl() skips current event and doesn't wrap"},
        {at(2, 2), NextEvent, at(2, 2), "Ensure next_event_impl() doesn't wrap"},
        {at(10, 0), NextEvent, at(10, 0), "Ensure next_event_impl() doesn't wrap (2)"},
        // prev_event_impl()
        {at(10, 0), PrevEvent, at(2, 1), "Ensure prev_event_impl() moves to prev pattern if current empty"},
        {at(2, 2), PrevEvent, at(2, 1), "Ensure prev_event_impl() works"},
        {at(2, 1), PrevEvent, at(1, 1), "Ensure prev_event_impl() skips current event"},
        {at(1, 1), PrevEvent, at(1, 1), "Ensure prev_event_impl() skips current event and doesn't wrap"},
        {at(1, 0), PrevEvent, at(1, 0), "Ensure prev_event_impl() doesn't wrap (1)"},
        {at(0, 0), PrevEvent, at(0, 0), "Ensure prev_event_impl() doesn't wrap (2)"},
    };


    for (TestCase test_case : test_cases) {
        CAPTURE(test_case.message);

        TickT out;
        if (test_case.which_function == NextEvent) {
            out = next_event_impl(document, Cursor{x, test_case.start});
        } else {
            out = prev_event_impl(document, Cursor{x, test_case.start});
        }

        CHECK(out == test_case.end);
    }
}

TEST_CASE("Test next_event_impl()/prev_event_impl() on empty document") {
    auto document = empty_doc();

    cursor::CursorX const x{0, 0};

    // Both functions should be no-op identity functions on empty documents,
    // and indicate that the cursor has wrapped.
    {
        TickT out = next_event_impl(document, Cursor{x, at(1, 2)});
        CHECK(out == at(1, 2));
    }
    {
        TickT out = prev_event_impl(document, Cursor{x, at(1, 2)});
        CHECK(out == at(1, 2));
    }
}

#if 0

TEST_CASE("Test prev_row_impl()/next_row_impl()") {
    auto document = empty_doc();
    int rows_per_beat = 4;

    MovementConfig move_cfg{
        .wrap_across_frames = true,
    };

    enum WhichFunction {
        PrevRow,
        NextRow,
    };

    struct TestCase {
        WhichFunction which_function;
        TickT start;
        TickT end;
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

        TickT out;
        if (test_case.which_function == NextRow) {
            out = next_row_impl(document, test_case.start, rows_per_beat, move_cfg);
        } else {
            out = prev_row_impl(document, test_case.start, rows_per_beat, move_cfg);
        }

        CHECK(out == TickT{test_case.end});
    }
}

TEST_CASE("Test prev_row_impl()/next_row_impl() wrapping") {
    // nbeats=4, npattern=4
    auto document = empty_doc();
    int rows_per_beat = 4;

    MovementConfig wrap_doc_1{
        .wrap_across_frames = true,
    };

    MovementConfig no_wrap{
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
        TickT start;
        TickT wrap_doc;
        TickT wrap_pattern;
        // If wrap is turned off, the position should be unchanged (= start).
        char const * message;
    };

    BeatFraction end = time(3, 3, 4);
    TestCase test_cases[] {
        {NextRow, {0, end}, {{1, 0}}, {0, 0}, "Ensure that next_row_impl() wraps properly"},
        {NextRow, {0, 100}, {{1, 0}}, {0, 0}, "Ensure that next_row_impl() wraps on out-of-bounds"},
        {NextRow, {3, end}, {{3, end}}, {3, 0}, "Ensure that next_row_impl() doesn't wrap around the document"},
        {PrevRow, {1, 0}, {{0, end}}, {1, end}, "Ensure that prev_row_impl() wraps properly"},
        {PrevRow, {1, -1}, {{0, end}}, {1, end}, "Ensure that prev_row_impl() wraps on out-of-bounds"},
        {PrevRow, {0, 0}, {{0, 0}}, {0, end}, "Ensure that prev_row_impl() doesn't wrap around the document"},
    };

    for (TestCase test_case : test_cases) {
        CAPTURE(test_case.message);

        auto verify = [&] (
            char const* move_name, MovementConfig const& move_cfg, TickT end
        ) {
            CAPTURE(move_name);
            TickT out;
            if (test_case.which_function == NextRow) {
                out = next_row_impl(document, test_case.start, rows_per_beat, move_cfg);
            } else {
                out = prev_row_impl(document, test_case.start, rows_per_beat, move_cfg);
            }

            CHECK(out == end);
        };

        verify("wrap_doc_1", wrap_doc_1, test_case.wrap_doc);
        verify("no_wrap", no_wrap, TickT{test_case.start});
    }
}
#endif

// TODO add tests for cursor_step

//TEST_CASE("Test move_up()/move_down() wrapping and events") {
//    // nbeats=4, npattern=4
//    auto document = empty_doc();
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

//    auto event_in_middle = empty_doc();
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
//        TickT start;
//        TickT end;
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

//        TickT out;
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
//            == TickT{0, row_0}
//        );
//    }

//    // Ensure that move_up() doesn't lock onto distant events
//    {
//        Cursor cursor{{0, 0}, {0, 0}};
//        CHECK(
//            move_up(event_in_middle, cursor, args, move_cfg)
//            == TickT{3, last_row}
//        );
//    }
//}

}

#endif
