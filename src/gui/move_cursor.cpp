#include "move_cursor.h"
#include "doc_util/kv.h"
#include "util/math.h"
#include "util/compare_impl.h"
#include "util/release_assert.h"

#include <algorithm>

namespace gui::move_cursor {

// # Utility functions.

/// Like gui::pattern_editor_panel::Column, but without list of subcolumns.
struct CursorChannel {
    chip_common::ChipIndex chip;
    chip_common::ChannelIndex channel;
};

using ChannelList = std::vector<CursorChannel>;

[[nodiscard]] ChannelList gen_channel_list(doc::Document const & document) {
    ChannelList channel_list;

    for (
        chip_common::ChipIndex chip_index = 0;
        chip_index < document.chips.size();
        chip_index++
    ) {
        for (
            chip_common::ChannelIndex channel_index = 0;
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

using doc_util::kv::KV;

/// When moving the cursor around,
/// we need to compare whether the next event or row is closer to the cursor.
///
/// Wrapping from the end to the beginning of the document
/// is logically "later" than the end of the document,
/// and returns MoveCursorResult{Wrap::Plus, begin}.
/// This compares greater than MoveCursorResult{Wrap::Zero, end}.
enum class Wrap {
    Minus = -1,
    None = 0,
    Plus = +1,
};

struct MoveCursorResult {
    Wrap wrapped;
    PatternAndBeat time;

    COMPARABLE(MoveCursorResult)
};

COMPARABLE_IMPL(MoveCursorResult, (self.wrapped, self.time))

[[nodiscard]] static
MoveCursorResult prev_event_impl(doc::Document const& document, cursor::Cursor cursor) {
    auto [chip, channel] = gen_channel_list(document)[cursor.x.column];

    doc::SeqEntryIndex const orig_seq_i = cursor.y.seq_entry_index;

    doc::SeqEntryIndex seq_i = orig_seq_i;
    auto wrapped = Wrap::None;

    auto const get_pattern =
        [&, chip=chip, channel=channel] () -> doc::EventList const&
    {
        auto & seq_entry = document.sequence[seq_i];
        return seq_entry.chip_channel_events[chip][channel];
    };

    auto advance_pattern = [&document, &seq_i, &wrapped] () {
        if (seq_i == 0) {
            wrapped = Wrap::Minus;
        }
        auto n = doc::SeqEntryIndex(document.sequence.size());
        seq_i = (seq_i + n - 1) % n;
    };

    auto get_return = [&] (doc::TimedRowEvent const& ev) {
        return MoveCursorResult{wrapped, PatternAndBeat{seq_i, ev.time.anchor_beat}};
    };

    using Rev = doc::EventList::const_reverse_iterator;

    {
        auto & event_list = get_pattern();

        // it's not UB, i swear
        auto const kv = KV{const_cast<doc::EventList &>(event_list)};
        auto x = Rev{kv.beat_begin(cursor.y.beat)};

        // Find nearest event before given time.
        if (x != event_list.rend()) {
            return get_return(*x);
        }
    }

    while (true) {
        // Mutates seq_i and wrapped.
        advance_pattern();
        auto & event_list = get_pattern();

        // Find last event.
        if (event_list.size() > 0) {
            return get_return(event_list[event_list.size() - 1]);
        }

        if (seq_i == orig_seq_i) {
            break;
        }
    }

    release_assert(wrapped == Wrap::Minus);
    return MoveCursorResult{Wrap::Minus, cursor.y};
}

[[nodiscard]] static
MoveCursorResult next_event_impl(doc::Document const& document, cursor::Cursor cursor) {
    auto [chip, channel] = gen_channel_list(document)[cursor.x.column];

    doc::SeqEntryIndex const orig_seq_i = cursor.y.seq_entry_index;

    doc::SeqEntryIndex seq_i = orig_seq_i;
    auto wrapped = Wrap::None;

    auto const get_pattern =
        [&, chip=chip, channel=channel] () -> doc::EventList const&
    {
        auto & seq_entry = document.sequence[seq_i];
        return seq_entry.chip_channel_events[chip][channel];
    };

    auto advance_pattern = [&document, &seq_i, &wrapped] () {
        auto n = doc::SeqEntryIndex(document.sequence.size());
        seq_i = (seq_i + 1) % n;
        if (seq_i == 0) {
            wrapped = Wrap::Plus;
        }
    };

    auto get_return = [&] (doc::TimedRowEvent const& ev) {
        return MoveCursorResult{
            wrapped, PatternAndBeat{seq_i, ev.time.anchor_beat}
        };
    };

    {
        auto & event_list = get_pattern();

        // it's not UB, i swear
        auto const kv = KV{const_cast<doc::EventList &>(event_list)};
        auto x = kv.beat_end(cursor.y.beat);

        // Find first event past given time.
        if (x != event_list.end()) {
            return get_return(*x);
        }
    }

    while (true) {
        // Mutates seq_i and wrapped.
        advance_pattern();
        auto & event_list = get_pattern();

        // Find first event.
        if (event_list.size() > 0) {
            return get_return(event_list[0]);
        }

        if (seq_i == orig_seq_i) {
            break;
        }
    }

    release_assert(wrapped == Wrap::Plus);
    return MoveCursorResult{Wrap::Plus, cursor.y};
}

PatternAndBeat prev_event(doc::Document const& document, cursor::Cursor cursor) {
    return prev_event_impl(document, cursor).time;
}

PatternAndBeat next_event(doc::Document const& document, cursor::Cursor cursor) {
    return next_event_impl(document, cursor).time;
}


// # Moving cursor by beats

using doc::SeqEntryIndex;
using doc::BeatFraction;
using doc::FractionInt;
using util::math::increment_mod;
using util::math::decrement_mod;
using util::math::frac_prev;
using util::math::frac_next;

// Move the cursor, snapping to the nearest row.

[[nodiscard]] static MoveCursorResult prev_row_impl(
    doc::Document const& document,
    PatternAndBeat cursor_y,
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
            if (cursor_y.seq_entry_index == 0) {
                wrapped = Wrap::Minus;
            }
            decrement_mod(
                cursor_y.seq_entry_index, (SeqEntryIndex)document.sequence.size()
            );
        }

        auto const & seq_entry = document.sequence[cursor_y.seq_entry_index];
        prev_row = frac_prev(seq_entry.nbeats * rows_per_beat);

    } else {
        // Don't move the cursor.
        return MoveCursorResult{Wrap::None, cursor_y};
    }

    cursor_y.beat = BeatFraction{prev_row, rows_per_beat};
    return MoveCursorResult{wrapped, cursor_y};
}

[[nodiscard]] static MoveCursorResult next_row_impl(
    doc::Document const& document,
    PatternAndBeat cursor_y,
    int rows_per_beat,
    MovementConfig const& move_cfg
) {
    BeatFraction const orig_row = cursor_y.beat * rows_per_beat;
    FractionInt const raw_next = frac_next(orig_row);
    FractionInt next_row;
    auto wrapped = Wrap::None;

    auto const & seq_entry = document.sequence[cursor_y.seq_entry_index];
    BeatFraction const num_rows = seq_entry.nbeats * rows_per_beat;

    if (raw_next < num_rows) {
        next_row = raw_next;

    } else if (move_cfg.wrap_across_frames || move_cfg.wrap_cursor) {
        if (move_cfg.wrap_across_frames) {
            increment_mod(
                cursor_y.seq_entry_index, (SeqEntryIndex)document.sequence.size()
            );
            if (cursor_y.seq_entry_index == 0) {
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

PatternAndBeat prev_beat(
    doc::Document const& document,
    PatternAndBeat cursor_y,
    MovementConfig const& move_cfg
) {
    return prev_row_impl(document, cursor_y, 1, move_cfg).time;
}

PatternAndBeat next_beat(
    doc::Document const& document,
    PatternAndBeat cursor_y,
    MovementConfig const& move_cfg
) {
    return next_row_impl(document, cursor_y, 1, move_cfg).time;
}


PatternAndBeat move_up(
    doc::Document const& document,
    cursor::Cursor cursor,
    MoveCursorYArgs const& args,
    MovementConfig const& move_cfg
) {
    // See doc comment in header for more docs.

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

PatternAndBeat move_down(
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

PatternAndBeat cursor_step(
    doc::Document const& document,
    cursor::Cursor cursor,
    MoveCursorYArgs const& args,
    MovementConfig const& move_cfg
) {
    // See doc comment in header for more docs.

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

}

#ifdef UNITTEST

#include <doctest.h>

#include "timing_common.h"
#include "doc.h"
#include "chip_kinds.h"
#include "doc_util/shorthand.h"

namespace gui::move_cursor {

using namespace doc;
using chip_kinds::ChipKind;
using namespace doc_util::shorthand;

static Document empty_doc(int n_seq_entry) {
    SequencerOptions sequencer_options{
        .ticks_per_beat = 10,
    };

    Sequence sequence;

    for (int seq_entry_idx = 0; seq_entry_idx < n_seq_entry; seq_entry_idx++) {
        sequence.push_back(SequenceEntry {
            .nbeats = 4,
            .chip_channel_events = {
                // chip 0
                {
                    // channel 0
                    {},
                    // channel 1
                    {},
                }
            },
        });
    }

    return DocumentCopy{
        .sequencer_options = sequencer_options,
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = Instruments(),
        .chips = {ChipKind::Apu1},
        .sequence = sequence
    };
}

using timing::PatternAndBeat;
using cursor::Cursor;

TEST_CASE("Test next_event_impl()/prev_event_impl()") {
    auto document = empty_doc(4);

    document.sequence[1].chip_channel_events[0][0].push_back({at(1), {1}});
    document.sequence[1].chip_channel_events[0][0].push_back({at(2), {2}});

    cursor::CursorX const x{0, 0};

    enum WhichFunction {
        PrevEvent,
        NextEvent,
    };

    struct TestCase {
        PatternAndBeat start;
        WhichFunction which_function;
        PatternAndBeat end;
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
        CHECK(out.time == PatternAndBeat{1, 2});
        CHECK(out.wrapped == Wrap::Plus);
    }
    {
        MoveCursorResult out = prev_event_impl(document, Cursor{x, {1, 2}});
        CHECK(out.time == PatternAndBeat{1, 2});
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
        PatternAndBeat start;
        PatternAndBeat end;
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
        PatternAndBeat start;
        MoveCursorResult wrap_doc;
        PatternAndBeat wrap_pattern;
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

TEST_CASE("Test move_up()/move_down() wrapping and events") {
    // nbeats=4, npattern=4
    auto document = empty_doc(4);
    MoveCursorYArgs args{.rows_per_beat = 4, .step = 1};

    MovementConfig move_cfg{
        .wrap_cursor = true,
        .wrap_across_frames = true,
    };

    BeatFraction row_0 = 0;
    BeatFraction mid_row_0 = time(0, 1, 8);
    BeatFraction row_1 = time(0, 1, 4);

    BeatFraction last_row = time(3, 3, 4);
    BeatFraction mid_last_row = time(3, 7, 8);


    // Add event midway through pattern 1, row 0.
    document.sequence[1].chip_channel_events[0][0].push_back(
        TimedRowEvent{TimeInPattern{time(0, 1, 8), 0}, RowEvent{0}}
    );
    // Add event midway through pattern 1, final row.
    document.sequence[1].chip_channel_events[0][0].push_back(
        TimedRowEvent{TimeInPattern{mid_last_row, 0}, RowEvent{0}}
    );
    // Add event midway through pattern 3, final row of document.
    document.sequence[3].chip_channel_events[0][0].push_back(
        TimedRowEvent{TimeInPattern{mid_last_row, 0}, RowEvent{0}}
    );

    auto event_in_middle = empty_doc(4);
    event_in_middle.sequence[2].chip_channel_events[0][0].push_back(
        TimedRowEvent{TimeInPattern{2, 0}, RowEvent{0}}
    );

    enum WhichFunction {
        MoveUp,
        MoveDown,
    };

    /*
    how many separate toggles/modes should I have for
    "enable end-of-document wrap" and "enable moving between patterns" and
    "wrap around top/bottom of a single pattern"?
    */
    struct TestCase {
        WhichFunction which_function;
        PatternAndBeat start;
        PatternAndBeat end;
        // If wrap is turned off, the position should be unchanged (= start).
        char const * message;
    };

    TestCase test_cases[] {
        {MoveDown, {0, row_0}, {0, row_1},
            "Ensure that move_down() works normally if no events present"},
        {MoveDown, {1, row_0}, {1, mid_row_0},
            "Ensure that move_down() locks onto mid-row events"},
        {MoveDown, {1, last_row}, {1, mid_last_row},
            "Ensure that move_down() locks onto mid-row events at the end of a pattern"},
        {MoveDown, {3, last_row}, {3, mid_last_row},
            "Ensure that move_down() locks onto mid-row events at the end of the document"},
        {MoveUp, {0, row_1}, {0, row_0},
            "Ensure that move_up() works normally if no events present"},
        {MoveUp, {1, row_1}, {1, mid_row_0},
            "Ensure that move_up() locks onto mid-row events"},
        {MoveUp, {2, row_0}, {1, mid_last_row},
            "Ensure that move_up() locks onto mid-row events at the end of a pattern"},
        {MoveUp, {0, row_0}, {3, mid_last_row},
            "Ensure that move_up() locks onto mid-row events at the end of the document"},
    };

    for (TestCase test_case : test_cases) {
        CAPTURE(test_case.message);

        Cursor cursor{{0, 0}, test_case.start};

        PatternAndBeat out;
        if (test_case.which_function == MoveDown) {
            out = move_down(document, cursor, args, move_cfg);
        } else {
            out = move_up(document, cursor, args, move_cfg);
        }

        CHECK(out == test_case.end);
    }

    // Ensure that move_down() doesn't lock onto distant events
    {
        Cursor cursor{{0, 0}, {3, last_row}};
        CHECK(
            move_down(event_in_middle, cursor, args, move_cfg)
            == PatternAndBeat{0, row_0}
        );
    }

    // Ensure that move_up() doesn't lock onto distant events
    {
        Cursor cursor{{0, 0}, {0, 0}};
        CHECK(
            move_up(event_in_middle, cursor, args, move_cfg)
            == PatternAndBeat{3, last_row}
        );
    }
}

}

#endif
