#include "document.h"

#include "audio/synth/chip_kinds_common.h"
#include "util/macros.h"

#ifdef UNITTEST
#include <doctest.h>
#endif

#include <cmath>

namespace doc {
inline namespace tuning {
    constexpr double NOTES_PER_OCTAVE = 12.;

    FrequenciesOwned equal_temperament(
        ChromaticInt root_chromatic, FreqDouble root_frequency
    ) {
        FrequenciesOwned out;
        for (size_t i = 0; i < CHROMATIC_COUNT; i++) {
            auto semitone_offset = (ptrdiff_t)i - (ptrdiff_t)root_chromatic;
            double freq_ratio = pow(2, (double)semitone_offset / NOTES_PER_OCTAVE);
            out[i] = freq_ratio * root_frequency;
        }
        return out;
    }
}

#ifdef UNITTEST
TEST_CASE("equal_temperament()") {
    FrequenciesOwned freqs = equal_temperament();
    CHECK(freqs[69] == 440.);
    CHECK(256 < freqs[60]);
    CHECK(freqs[60] < 512);
}
#endif

Document dummy_document() {
    using Frac = BeatFraction;

    Document::ChipList::transient_type chips;
    SequenceEntry::ChipChannelEvents::transient_type chip_channel_events;

    // chip 0
    {
        auto const chip_kind = chip_kinds::ChipKind::Apu1;
        using ChannelID = chip_kinds::Apu1ChannelID;

        chips.push_back(chip_kind);
        chip_channel_events.push_back([]() {
            SequenceEntry::ChannelToEvents::transient_type channel_events;

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
