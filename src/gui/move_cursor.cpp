#include "move_cursor.h"
#include "edit_util/kv.h"
#include "util/math.h"

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

using edit_util::kv::KV;

SwitchEventResult prev_event(doc::Document const& document, cursor::Cursor cursor) {
    auto [chip, channel] = gen_channel_list(document)[cursor.x.column];

    doc::SeqEntryIndex const orig_seq_i = cursor.y.seq_entry_index;

    doc::SeqEntryIndex seq_i = orig_seq_i;
    bool wrapped = false;

    auto const get_pattern =
        [&, chip=chip, channel=channel] () -> doc::EventList const&
    {
        auto & seq_entry = document.sequence[seq_i];
        return seq_entry.chip_channel_events[chip][channel];
    };

    auto advance_pattern = [&document, &seq_i, &wrapped] () {
        if (seq_i == 0) {
            wrapped = true;
        }
        auto n = doc::SeqEntryIndex(document.sequence.size());
        seq_i = (seq_i + n - 1) % n;
    };

    auto get_return = [&] (doc::TimedRowEvent const& ev) {
        return SwitchEventResult{
            PatternAndBeat{seq_i, ev.time.anchor_beat}, wrapped
        };
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

    return SwitchEventResult{cursor.y, true};
}

SwitchEventResult next_event(doc::Document const& document, cursor::Cursor cursor) {
    auto [chip, channel] = gen_channel_list(document)[cursor.x.column];

    doc::SeqEntryIndex const orig_seq_i = cursor.y.seq_entry_index;

    doc::SeqEntryIndex seq_i = orig_seq_i;
    bool wrapped = false;

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
            wrapped = true;
        }
    };

    auto get_return = [&] (doc::TimedRowEvent const& ev) {
        return SwitchEventResult{
            PatternAndBeat{seq_i, ev.time.anchor_beat}, wrapped
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

    return SwitchEventResult{cursor.y, true};
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

[[nodiscard]] static PatternAndBeat prev_row(
    doc::Document const& document,
    PatternAndBeat cursor_y,
    int rows_per_beat,
    MovementConfig const& move_cfg
) {
    BeatFraction const orig_row = cursor_y.beat * rows_per_beat;
    FractionInt const raw_prev = frac_prev(orig_row);
    FractionInt prev_row;

    if (raw_prev >= 0) {
        prev_row = raw_prev;

    } else if (move_cfg.wrap_across_frames || move_cfg.wrap_cursor) {
        if (move_cfg.wrap_across_frames) {
            decrement_mod(
                cursor_y.seq_entry_index, (SeqEntryIndex)document.sequence.size()
            );
        }

        auto const & seq_entry = document.sequence[cursor_y.seq_entry_index];
        prev_row = frac_prev(seq_entry.nbeats * rows_per_beat);

    } else {
        // Don't move the cursor.
        return cursor_y;
    }

    cursor_y.beat = BeatFraction{prev_row, rows_per_beat};
    return cursor_y;
}

[[nodiscard]] static PatternAndBeat next_row(
    doc::Document const& document,
    PatternAndBeat cursor_y,
    int rows_per_beat,
    MovementConfig const& move_cfg
) {
    BeatFraction const orig_row = cursor_y.beat * rows_per_beat;
    FractionInt const raw_next = frac_next(orig_row);
    FractionInt next_row;

    auto const & seq_entry = document.sequence[cursor_y.seq_entry_index];
    BeatFraction const num_rows = seq_entry.nbeats * rows_per_beat;

    if (raw_next < num_rows) {
        next_row = raw_next;

    } else if (move_cfg.wrap_across_frames || move_cfg.wrap_cursor) {
        if (move_cfg.wrap_across_frames) {
            increment_mod(
                cursor_y.seq_entry_index, (SeqEntryIndex)document.sequence.size()
            );
        }

        next_row = 0;

    } else {
        // Don't move the cursor.
        return cursor_y;
    }

    cursor_y.beat = BeatFraction{next_row, rows_per_beat};
    return cursor_y;
}

PatternAndBeat prev_beat(
    doc::Document const& document,
    PatternAndBeat cursor_y,
    MovementConfig const& move_cfg
) {
    return prev_row(document, cursor_y, 1, move_cfg);
}

PatternAndBeat next_beat(
    doc::Document const& document,
    PatternAndBeat cursor_y,
    MovementConfig const& move_cfg
) {
    return next_row(document, cursor_y, 1, move_cfg);
}

}

#ifdef UNITTEST

#include <doctest.h>

#include "timing_common.h"
#include "doc.h"
#include "chip_kinds.h"
#include "edit_util/shorthand.h"

namespace gui::move_cursor {

using namespace doc;
using chip_kinds::ChipKind;
using namespace edit_util::shorthand;

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

TEST_CASE("Test next_event()/prev_event()") {
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
        bool wrapped;
        char const * message;
    };

    TestCase test_cases[] {
        // next_event()
        {{0, 0}, NextEvent, {1, 1}, false, "Ensure next_event() moves to next pattern if current empty"},
        {{1, 0}, NextEvent, {1, 1}, false, "Ensure next_event() works"},
        {{1, 1}, NextEvent, {1, 2}, false, "Ensure next_event() skips current event"},
        {{1, 2}, NextEvent, {1, 1}, true, "Ensure next_event() skips current event and wraps to same pattern"},
        {{1, 3}, NextEvent, {1, 1}, true, "Ensure next_event() wraps to same pattern"},
        {{2, 0}, NextEvent, {1, 1}, true, "Ensure next_event() wraps"},
        // prev_event()
        {{2, 0}, PrevEvent, {1, 2}, false, "Ensure prev_event() moves to next pattern if current empty"},
        {{1, 3}, PrevEvent, {1, 2}, false, "Ensure prev_event() works"},
        {{1, 2}, PrevEvent, {1, 1}, false, "Ensure prev_event() skips current event"},
        {{1, 1}, PrevEvent, {1, 2}, true, "Ensure prev_event() skips current event and wraps to same pattern"},
        {{1, 0}, PrevEvent, {1, 2}, true, "Ensure prev_event() wraps to same pattern"},
        {{0, 0}, PrevEvent, {1, 2}, true, "Ensure prev_event() wraps"},
    };

    for (TestCase test_case : test_cases) {
        CAPTURE(test_case.message);

        SwitchEventResult out;
        if (test_case.which_function == NextEvent) {
            out = next_event(document, Cursor{x, test_case.start});
        } else {
            out = prev_event(document, Cursor{x, test_case.start});
        }

        CHECK(out.time == test_case.end);
        CHECK(out.wrapped == test_case.wrapped);
    }
}

TEST_CASE("Test next_event()/prev_event() on empty document") {
    auto document = empty_doc(4);

    cursor::CursorX const x{0, 0};

    // Both functions should be no-op identity functions on empty documents,
    // and indicate that the cursor has wrapped.
    {
        SwitchEventResult out = next_event(document, Cursor{x, {1, 2}});
        CHECK(out.time == PatternAndBeat{1, 2});
        CHECK_UNARY(out.wrapped);
    }
    {
        SwitchEventResult out = prev_event(document, Cursor{x, {1, 2}});
        CHECK(out.time == PatternAndBeat{1, 2});
        CHECK_UNARY(out.wrapped);
    }
}

static BeatFraction time(int start, int num, int den) {
    return start + BeatFraction(num, den);
}

TEST_CASE("Test prev_row()/next_row()") {
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
        // next_row()
        {NextRow, {0, {0, 4}}, {0, {1, 4}}, "next_row() 0, 0/4"},
        {NextRow, {0, {1, 8}}, {0, {1, 4}}, "next_row() 0, 1/8"},
        {NextRow, {0, {1, 4}}, {0, {2, 4}}, "next_row() 0, 1/4"},
        // prev_row()
        {PrevRow, {0, {2, 4}}, {0, {1, 4}}, "prev_row() 0, 2/4"},
        {PrevRow, {0, {3, 8}}, {0, {1, 4}}, "prev_row() 0, 3/8"},
        {PrevRow, {0, {1, 4}}, {0, {0, 4}}, "prev_row() 0, 1/4"},
    };

    for (TestCase test_case : test_cases) {
        CAPTURE(test_case.message);

        PatternAndBeat out;
        if (test_case.which_function == NextRow) {
            out = next_row(document, test_case.start, rows_per_beat, move_cfg);
        } else {
            out = prev_row(document, test_case.start, rows_per_beat, move_cfg);
        }

        CHECK(out == test_case.end);
    }
}

TEST_CASE("Test prev_row()/next_row() wrapping") {
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
        PatternAndBeat wrap_doc;
        PatternAndBeat wrap_pattern;
        // If wrap is turned off, the position should be unchanged (= start).
        char const * message;
    };

    BeatFraction end = time(3, 3, 4);
    TestCase test_cases[] {
        {NextRow, {0, end}, {1, 0}, {0, 0}, "Ensure that next_row() wraps properly"},
        {NextRow, {0, 100}, {1, 0}, {0, 0}, "Ensure that next_row() wraps on out-of-bounds"},
        {NextRow, {3, end}, {0, 0}, {3, 0}, "Ensure that next_row() wraps around the document"},
        {PrevRow, {1, 0}, {0, end}, {1, end}, "Ensure that prev_row() wraps properly"},
        {PrevRow, {1, -1}, {0, end}, {1, end}, "Ensure that prev_row() wraps on out-of-bounds"},
        {PrevRow, {0, 0}, {3, end}, {0, end}, "Ensure that prev_row() wraps around the document"},
    };

    for (TestCase test_case : test_cases) {
        CAPTURE(test_case.message);

        auto verify = [&] (
            char const* move_name, MovementConfig const& move_cfg, PatternAndBeat end
        ) {
            CAPTURE(move_name);
            PatternAndBeat out;
            if (test_case.which_function == NextRow) {
                out = next_row(document, test_case.start, rows_per_beat, move_cfg);
            } else {
                out = prev_row(document, test_case.start, rows_per_beat, move_cfg);
            }

            CHECK(out == end);
        };

        #define VERIFY(MOVE_CFG, END)  verify(#MOVE_CFG, MOVE_CFG, END)
        VERIFY(wrap_doc_1, test_case.wrap_doc);
        VERIFY(wrap_doc_2, test_case.wrap_doc);
        VERIFY(wrap_pattern, test_case.wrap_pattern);
        VERIFY(no_wrap, test_case.start);
    }

}

}

#endif
