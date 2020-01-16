#include "doc.h"

#include "chip_kinds.h"
#include "util/release_assert.h"

namespace doc {

Document dummy_document() {
    using Frac = BeatFraction;

    Document::ChipList::transient_type chips;
    ChipChannelTo<EventList>::transient_type chip_channel_events;

    // chip 0
    {
        auto const chip_kind = chip_kinds::ChipKind::Apu1;
        using ChannelID = chip_kinds::Apu1ChannelID;

        chips.push_back(chip_kind);
        chip_channel_events.push_back([]() {
            ChannelTo<EventList>::transient_type channel_events;

            channel_events.push_back([]() {
                // TimeInPattern, RowEvent
                EventList events = KV{{}}
                    .set_time({0, 0}, {60})
                    .set_time({{1, 3}, 0}, {62})
                    .set_time({{2, 3}, 0}, {64})
                    .set_time({1, 0}, {65})
                    .set_time({1 + Frac{2, 3}, 0}, {62})
                    .event_list;
                return events;
            }());

            channel_events.push_back([]() {
                EventList events = KV{{}}
                    .set_time({2, 0}, {48})
                    .set_time({2 + Frac{1, 4}, 0}, {NOTE_CUT})
                    .set_time({2 + Frac{2, 4}, 0}, {44})
                    .set_time({2 + Frac{3, 4}, 0}, {NOTE_CUT})
                    .set_time({3, -2}, {39})
                    .set_time({3, 0}, {40})
                    .event_list;
                return events;
            }());

            release_assert(channel_events.size() == (int)ChannelID::COUNT);
            return channel_events.persistent();
        }());
    }

    return Document {
        .chips = chips.persistent(),
        .pattern = SequenceEntry {
            .nbeats = 4,
            .chip_channel_events = chip_channel_events.persistent(),
        },
        .sequencer_options = SequencerOptions{
            .ticks_per_beat = 24,
        },
        .frequency_table = equal_temperament(),
    };
}

}
