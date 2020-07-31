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

}

#endif
