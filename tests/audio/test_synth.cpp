#include "audio/synth.h"
#include "audio/synth/music_driver/driver_2a03.h"
#include "document.h"

#include <vector>

#include "doctest.h"

using audio::synth::music_driver::driver_2a03::Apu1Driver;


class GetDocumentStub : public doc::GetDocument {
    doc::Document document;

public:
    GetDocumentStub(doc::Document document) : document(document) {}

    doc::Document const & get_document() const override {
        return document;
    }
};

enum class TestChannelID {
    Pulse1,
    Pulse2,
};

static doc::Document one_note_document(TestChannelID which_channel, doc::Note pitch) {
    using namespace doc;

    Document::ChipList::transient_type chips;
    SequenceEntry::ChipChannelEvents::transient_type chip_channel_events;

    // chip 0
    {
        auto const chip_kind = chip_kinds::ChipKind::Apu1;
        using ChannelID = chip_kinds::Apu1ChannelID;

        chips.push_back(chip_kind);
        chip_channel_events.push_back([&]() {
            SequenceEntry::ChannelToEvents::transient_type channel_events;

            EventList one_note = [&]() {
                // .set_time(TimeInPattern, RowEvent)
                EventList events = KV{{}}
                    .set_time({0, 0}, {pitch})
                    .event_list;
                return events;
            }();

            EventList blank {};

            channel_events.push_back(
                which_channel == TestChannelID::Pulse1 ? one_note : blank
            );
            channel_events.push_back(
                which_channel == TestChannelID::Pulse2 ? one_note : blank
            );

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

/// Previously Apu1Driver/Apu1PulseDriver would not write registers upon startup,
/// unless incoming notes set them to a nonzero value.
/// When playing high notes (period <= 0xff),
/// no sound would come out unless a low note played first.
TEST_CASE("Test that high notes (with upper 3 bits zero) produce sound") {
    using audio::Amplitude;

    TestChannelID which_channel;
    SUBCASE("") { which_channel = TestChannelID::Pulse1; }
    SUBCASE("") { which_channel = TestChannelID::Pulse2; }
    CAPTURE((int) which_channel);

    doc::Note high_note{72};
    GetDocumentStub get_document{one_note_document(which_channel, high_note)};

    Apu1Driver driver{
        audio::synth::CPU_CLK_PER_S, get_document.get_document().frequency_table
    };

    // Pick `high_note` that we know to have a period register <= 0xff.
    // This ensures that the period register's high bits are all 0.
    CHECK(driver._tuning_table[high_note.value] <= 0xff);

    // (int stereo_nchan, int smp_per_s, doc::GetDocument &/*'a*/ get_document)
    audio::synth::OverallSynth synth{1, 48000, get_document};

    std::vector<Amplitude> buffer;
    buffer.resize(4 * 1024);
    synth.synthesize_overall(/*mut*/ buffer, buffer.size());

    bool positive_found = false;
    bool negative_found = false;
    Amplitude const THRESHOLD = 1000;

    for (audio::Amplitude y : buffer) {
        if (y >= THRESHOLD) {
            positive_found = true;
        }
        if (y <= -THRESHOLD) {
            negative_found = true;
        }
        if (positive_found && negative_found) {
            break;
        }
    }
    CHECK_UNARY(positive_found);
    CHECK_UNARY(negative_found);

    // TODO add a FFT-based test to ensure the right frequency is output.
}
