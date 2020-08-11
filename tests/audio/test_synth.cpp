#include "audio/synth.h"
#include "audio/synth/nes_2a03_driver.h"
#include "doc.h"
#include "doc_util/kv.h"
#include "chip_kinds.h"
#include "cmd_queue.h"
#include "timing_common.h"
#include "test_utils/parameterize.h"

#include <fmt/core.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include <utility>  // std::move

#include "doctest.h"

using audio::synth::nes_2a03_driver::Apu1Driver;
using audio::synth::nes_2a03_driver::Apu1PulseDriver;


enum class TestChannelID {
    NONE,
    Pulse1,
    Pulse2,
};

static doc::Document one_note_document(TestChannelID which_channel, doc::Note pitch) {
    using namespace doc;
    using doc_util::kv::KV;

    ChipList chips;
    ChipChannelTo<EventList> chip_channel_events;

    // chip 0
    {
        auto const chip_kind = chip_kinds::ChipKind::Apu1;
        using ChannelID = chip_kinds::Apu1ChannelID;

        chips.push_back(chip_kind);
        chip_channel_events.push_back([&]() {
            ChannelTo<EventList> channel_events;

            EventList one_note {{{0, 0}, {pitch}}};
            EventList blank {};

            channel_events.push_back(
                which_channel == TestChannelID::Pulse1 ? one_note : blank
            );
            channel_events.push_back(
                which_channel == TestChannelID::Pulse2 ? one_note : blank
            );

            release_assert(channel_events.size() == (int)ChannelID::COUNT);
            return channel_events;
        }());
    }

    return DocumentCopy {
        .sequencer_options = SequencerOptions{
            .ticks_per_beat = 24,
        },
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .instruments = {},
        .chips = chips,
        .sequence = {
            SequenceEntry {
                .nbeats = 4,
                .chip_channel_events = chip_channel_events,
            }
        },
    };
}

using audio::Amplitude;
using audio::AudioOptions;
using audio::ClockT;
using cmd_queue::AudioCommand;
using cmd_queue::CommandQueue;

CommandQueue play_from_begin() {
    CommandQueue out;
    out.push(cmd_queue::SeekTo{timing::PatternAndBeat{0, 0}});
    return out;
}

/// Constructs a new OverallSynth at the specified sampling rate,
/// and runs it for the specified amount of time.
/// Returns the generated audio.
std::vector<Amplitude> run_new_synth(
    doc::Document const & document,
    uint32_t smp_per_s,
    blip_nsamp_t nsamp,
    AudioCommand * stub_command,
    AudioOptions audio_options
) {
    CAPTURE(smp_per_s);
    CAPTURE(nsamp);

    // (int stereo_nchan, int smp_per_s, locked_doc::GetDocument &/*'a*/ document)
    audio::synth::OverallSynth synth{
        1, smp_per_s, document.clone(), stub_command, audio_options
    };

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

PARAMETERIZE(all_audio_options, AudioOptions, audio_options,
    OPTION(audio_options.clocks_per_sound_update, 1);
    OPTION(audio_options.clocks_per_sound_update, 2);
    OPTION(audio_options.clocks_per_sound_update, 4);
    OPTION(audio_options.clocks_per_sound_update, 8);
    OPTION(audio_options.clocks_per_sound_update, 16);
)

PARAMETERIZE(all_channels, TestChannelID, which_channel,
    OPTION(which_channel, TestChannelID::Pulse1);
    OPTION(which_channel, TestChannelID::Pulse2);
)

/// Previously Apu1Driver/Apu1PulseDriver would not write registers upon startup,
/// unless incoming notes set them to a nonzero value.
/// When playing high notes (period <= 0xff),
/// no sound would come out unless a low note played first.
TEST_CASE("Test that empty documents produce silence") {
    AudioOptions audio_options;
    PICK(all_audio_options(audio_options));

    TestChannelID which_channel = TestChannelID::NONE;
    doc::Note random_note{60};
    doc::Document document{one_note_document(which_channel, random_note)};
    CommandQueue play_commands = play_from_begin();

    std::vector<Amplitude> buffer = run_new_synth(
        document, 48000, 4 * 1024, play_commands.begin(), audio_options
    );
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
    AudioOptions audio_options;
    CommandQueue play_commands = play_from_begin();

    PICK(all_channels(which_channel, all_audio_options(audio_options)));

    doc::Note high_note{72};
    doc::Document document{one_note_document(which_channel, high_note)};

    Apu1Driver driver{
        audio::synth::CLOCKS_PER_S, document.frequency_table
    };

    // Pick `high_note` that we know to have a period register <= 0xff.
    // This ensures that the period register's high bits are all 0.
    CHECK(driver._tuning_table[(size_t) high_note.value] <= 0xff);

    std::vector<Amplitude> buffer = run_new_synth(
        document, 48000, 4 * 1024, play_commands.begin(), audio_options
    );
    Amplitude const THRESHOLD = 1000;
    check_signed_amplitude(buffer, THRESHOLD);
}

/// The 2A03's sweep unit will silence the bottom octave of notes,
/// unless the negate flag is set.
/// Make sure the bottom octave produces sound.
TEST_CASE("Test that low notes (with uppermost bit set) produce sound") {

    TestChannelID which_channel;
    AudioOptions audio_options;

    PICK(all_channels(which_channel, all_audio_options(audio_options)));

    doc::Note low_note{36};
    doc::Document document{one_note_document(which_channel, low_note)};
    CommandQueue play_commands = play_from_begin();

    Apu1Driver driver{
        audio::synth::CLOCKS_PER_S, document.frequency_table
    };

    // Pick `low_note` that we know to be in the bottom octave of notes.
    CHECK(
        driver._tuning_table[(size_t) low_note.value]
        >= (Apu1PulseDriver::MAX_PERIOD + 1) / 2
    );

    std::vector<Amplitude> buffer = run_new_synth(
        document, 48000, 4 * 1024, play_commands.begin(), audio_options
    );
    Amplitude const THRESHOLD = 1000;
    check_signed_amplitude(buffer, THRESHOLD);
}

TEST_CASE("Send random values into AudioInstance and look for assertion errors") {

    doc::Note note{60};
    doc::Document document{one_note_document(TestChannelID::Pulse1, note)};
    CommandQueue play_commands = play_from_begin();

    Apu1Driver driver{
        audio::synth::CLOCKS_PER_S, document.frequency_table
    };

    AudioOptions audio_options;
    PICK(all_audio_options(audio_options));

#define INCREASE(x)  x = (x) * 3 / 2 + 3

    // Setting smp_per_s to small numbers breaks blip_buffer's internal invariants.
    // >assert( length_ == msec ); // ensure length is same as that passed in
    // The largest failing value is 873.
    // Not all values fail. As smp_per_s decreases, it becomes more likely to fail.
    for (uint32_t smp_per_s = 1000; smp_per_s <= 250'000; INCREASE(smp_per_s)) {
        // smp_per_s * 0.25 second
        run_new_synth(document, smp_per_s, smp_per_s / 4, play_commands.begin(), audio_options);
    }

    // 44100Hz, zero samples
    run_new_synth(document, 44100, 0, play_commands.begin(), audio_options);

    // 48000Hz, various durations
    for (uint32_t nsamp = 1; nsamp <= 100'000; INCREASE(nsamp)) {
        run_new_synth(document, 48000, nsamp, play_commands.begin(), audio_options);
    }
}

TEST_CASE("Send all note pitches into AudioInstance and look for assertion errors") {
    // 32000Hz, 4000 samples, various note pitches.
    CommandQueue play_commands = play_from_begin();
    for (doc::ChromaticInt pitch = 0; pitch < doc::CHROMATIC_COUNT; pitch++) {
        doc::Document document{
            one_note_document(TestChannelID::Pulse1, {pitch})
        };
        run_new_synth(
            document,
            32000,
            1000,
            play_commands.begin(),
            AudioOptions{.clocks_per_sound_update = 1}
        );
    }
}

// TODO add RapidCheck for randomized testing?
