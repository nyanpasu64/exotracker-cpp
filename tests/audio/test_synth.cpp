#include "audio/synth.h"
#include "audio/synth/music_driver/driver_2a03.h"
#include "document.h"

#include <iostream>
#include <vector>

#include "doctest.h"

using audio::synth::music_driver::driver_2a03::Apu1Driver;
using audio::synth::music_driver::driver_2a03::Apu1PulseDriver;


class GetDocumentStub : public doc::GetDocument {
    doc::Document document;

public:
    GetDocumentStub(doc::Document document) : document(document) {}

    doc::Document const & get_document() const override {
        return document;
    }
};

enum class TestChannelID {
    NONE,
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

using audio::Amplitude;

/// Constructs a new OverallSynth at the specified sampling rate,
/// and runs it for the specified amount of time.
/// Returns the generated audio.
std::vector<Amplitude> run_new_synth(
    GetDocumentStub & get_document, int smp_per_s, int nsamp
) {
    CAPTURE(smp_per_s);
    CAPTURE(nsamp);

    // (int stereo_nchan, int smp_per_s, doc::GetDocument &/*'a*/ get_document)
    audio::synth::OverallSynth synth{1, smp_per_s, get_document};

    std::vector<Amplitude> buffer;
    buffer.resize(nsamp);
    synth.synthesize_overall(/*mut*/ buffer, buffer.size());

    return buffer;
};

void check_signed_amplitude(gsl::span<Amplitude> buffer, Amplitude threshold) {
    bool positive_found = false;
    bool negative_found = false;

    for (audio::Amplitude y : buffer) {
        if (y >= threshold) {
            positive_found = true;
        }
        if (y <= -threshold) {
            negative_found = true;
        }
        if (positive_found && negative_found) {
            break;
        }
    }
    CHECK_UNARY(positive_found);
    CHECK_UNARY(negative_found);

    // TODO add a FFT/autocorrelation test to ensure the peak lies at the right frequency.
    // This will require importing a FFT library.
}

/// Previously Apu1Driver/Apu1PulseDriver would not write registers upon startup,
/// unless incoming notes set them to a nonzero value.
/// When playing high notes (period <= 0xff),
/// no sound would come out unless a low note played first.
TEST_CASE("Test that empty documents produce silence") {

    TestChannelID which_channel = TestChannelID::NONE;
    doc::Note random_note{60};
    GetDocumentStub get_document{one_note_document(which_channel, random_note)};

    std::vector<Amplitude> buffer = run_new_synth(get_document, 48000, 4 * 1024);
    for (size_t idx = 0; idx < buffer.size(); idx++) {
        Amplitude y = buffer[idx];
        if (y != 0) {
            CAPTURE(idx);
            CHECK(y == 0);
        }
    }
}

/// Previously Apu1Driver/Apu1PulseDriver would not write registers upon startup,
/// unless incoming notes set them to a nonzero value.
/// When playing high notes (period <= 0xff),
/// no sound would come out unless a low note played first.
TEST_CASE("Test that high notes (with upper 3 bits zero) produce sound") {

    TestChannelID which_channel;
    SUBCASE("") { which_channel = TestChannelID::Pulse1; }
    SUBCASE("") { which_channel = TestChannelID::Pulse2; }
    CAPTURE((int) which_channel);

    doc::Note high_note{72};
    GetDocumentStub get_document{one_note_document(which_channel, high_note)};

    Apu1Driver driver{
        audio::synth::CLOCKS_PER_S, get_document.get_document().frequency_table
    };

    // Pick `high_note` that we know to have a period register <= 0xff.
    // This ensures that the period register's high bits are all 0.
    CHECK(driver._tuning_table[high_note.value] <= 0xff);

    std::vector<Amplitude> buffer = run_new_synth(get_document, 48000, 4 * 1024);
    Amplitude const THRESHOLD = 1000;
    check_signed_amplitude(buffer, THRESHOLD);
}

/// The 2A03's sweep unit will silence the bottom octave of notes,
/// unless the negate flag is set.
/// Make sure the bottom octave produces sound.
TEST_CASE("Test that low notes (with uppermost bit set) produce sound") {

    TestChannelID which_channel;
    SUBCASE("") { which_channel = TestChannelID::Pulse1; }
    SUBCASE("") { which_channel = TestChannelID::Pulse2; }
    CAPTURE((int) which_channel);

    doc::Note low_note{36};
    GetDocumentStub get_document{one_note_document(which_channel, low_note)};

    Apu1Driver driver{
        audio::synth::CLOCKS_PER_S, get_document.get_document().frequency_table
    };

    // Pick `low_note` that we know to be in the bottom octave of notes.
    CHECK(
        driver._tuning_table[low_note.value] >= (Apu1PulseDriver::MAX_PERIOD + 1) / 2
    );

    std::vector<Amplitude> buffer = run_new_synth(get_document, 48000, 16 * 1024);
    Amplitude const THRESHOLD = 1000;
    check_signed_amplitude(buffer, THRESHOLD);
}

TEST_CASE("Send random values into AudioInstance and look for assertion errors") {

    doc::Note note{60};
    GetDocumentStub get_document{one_note_document(TestChannelID::Pulse1, note)};

    Apu1Driver driver{
        audio::synth::CLOCKS_PER_S, get_document.get_document().frequency_table
    };

#define INCREASE(x)  x = (x) * 3 / 2 + 3

    // Setting smp_per_s to small numbers breaks blip_buffer's internal invariants.
    // >assert( length_ == msec ); // ensure length is same as that passed in
    // The largest failing value is 873.
    // Not all values fail. As smp_per_s decreases, it becomes more likely to fail.
    for (int smp_per_s = 1000; smp_per_s <= 250'000; INCREASE(smp_per_s)) {
        run_new_synth(get_document, smp_per_s, smp_per_s / 4);  // smp_per_s * 0.25 second
    }

    // 44100Hz, zero samples
    run_new_synth(get_document, 44100, 0);

    // 48000Hz, various durations
    for (int nsamp = 1; nsamp <= 100'000; INCREASE(nsamp)) {
        run_new_synth(get_document, 48000, nsamp);
    }
}

TEST_CASE("Send all note pitches into AudioInstance and look for assertion errors") {
    // 32000Hz, 4000 samples, various note pitches.
    for (doc::ChromaticInt pitch = 0; pitch < doc::CHROMATIC_COUNT; pitch++) {
        GetDocumentStub get_document{
            one_note_document(TestChannelID::Pulse1, {pitch})
        };
        run_new_synth(get_document, 32000, 4000);
    }
}

// TODO add RapidCheck for randomized testing?
