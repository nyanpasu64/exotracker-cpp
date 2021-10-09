#include "audio/synth.h"
#include "audio/synth/chip_instance_common.h"
#include "audio/synth/spc700_driver.h"
#include "doc.h"
#include "chip_kinds.h"
#include "cmd_queue.h"
#include "timing_common.h"
#include "doc_util/sample_instrs.h"
#include "edit/edit_sample_list.h"
#include "test_utils/parameterize.h"

#include <fmt/core.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
#include <utility>  // std::move

#include <doctest.h>

using audio::synth::NsampT;
using audio::synth::spc700_driver::Spc700Driver;
using audio::synth::spc700_driver::Spc700ChannelDriver;
using chip_kinds::Spc700ChannelID;
using std::move;

using namespace doc_util::sample_instrs;


using MaybeChannelID = std::optional<Spc700ChannelID>;

static doc::Document one_note_document(MaybeChannelID which_channel, doc::Note pitch) {
    using namespace doc;

    Samples samples;
    samples[0] = pulse_50();

    Instruments instruments;
    instruments[0] = Instrument {
        .name = "50%",
        .keysplit = {InstrumentPatch {
            .sample_idx = 0,
            .adsr = INFINITE,
        }}
    };

    ChipList chips{ChipKind::Spc700};

    Timeline timeline;

    timeline.push_back([&]() -> TimelineFrame {
        EventList one_note {TimedRowEvent{0, RowEvent{pitch, 0}}};
        EventList blank {};

        std::vector<TimelineCell> channel_cells;
        for (size_t i = 0; i < enum_count<Spc700ChannelID>; i++) {
            if (which_channel == Spc700ChannelID(i)) {
                channel_cells.push_back(TimelineCell{TimelineBlock::from_events(one_note)});
            } else {
                channel_cells.push_back(TimelineCell{});
            }
        }

        return TimelineFrame {
            .nbeats = 4,
            .chip_channel_cells = {move(channel_cells)},
        };
    }());

    return DocumentCopy {
        .sequencer_options = SequencerOptions{
            .target_tempo = 100,
        },
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = move(samples),
        .instruments = move(instruments),
        .chips = chips,
        .chip_channel_settings = spc_chip_channel_settings(),
        .timeline = move(timeline),
    };
}

using audio::Amplitude;
using audio::AudioOptions;
using audio::ClockT;
using cmd_queue::AudioCommand;
using cmd_queue::CommandQueue;

/// The majority of the entire exotracker test suite was not spent in driver logic
/// or S-DSP emulation, but in libsamplerate's sinc interpolation.
/// Using a faster resampler mode reduces the debug-mode test runtime by over 50%.
/// And ZOH has the useful property that it preserves the exact amplitudes
/// coming from the S-DSP.
constexpr AudioOptions FAST_RESAMPLER = {
    .resampler_quality = SRC_ZERO_ORDER_HOLD,
};

static CommandQueue play_from_begin() {
    CommandQueue out;
    out.push(cmd_queue::PlayFrom{timing::GridAndBeat{0, 0}});
    return out;
}

/// Constructs a new OverallSynth at the specified sampling rate,
/// and runs it for the specified amount of time.
/// Returns the generated audio.
static std::vector<Amplitude> run_new_synth(
    doc::Document const & document,
    uint32_t smp_per_s,
    NsampT nsamp,
    AudioCommand * stub_command)
{
    using audio::synth::STEREO_NCHAN;

    CAPTURE(smp_per_s);
    CAPTURE(nsamp);

    audio::synth::OverallSynth synth{
        STEREO_NCHAN, smp_per_s, document.clone(), stub_command, FAST_RESAMPLER
    };

    std::vector<Amplitude> buffer;
    buffer.resize(nsamp * STEREO_NCHAN);
    synth.synthesize_overall(/*mut*/ buffer, nsamp);

    return buffer;
};

static void check_signed_amplitude(gsl::span<Amplitude> buffer, Amplitude threshold) {
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

PARAMETERIZE(all_channels, Spc700ChannelID, which_channel,
    OPTION(which_channel, Spc700ChannelID::Channel1);
    OPTION(which_channel, Spc700ChannelID::Channel2);
    OPTION(which_channel, Spc700ChannelID::Channel3);
    OPTION(which_channel, Spc700ChannelID::Channel4);
    OPTION(which_channel, Spc700ChannelID::Channel5);
    OPTION(which_channel, Spc700ChannelID::Channel6);
    OPTION(which_channel, Spc700ChannelID::Channel7);
    OPTION(which_channel, Spc700ChannelID::Channel8);
)

TEST_CASE("Test that not beginning playback produces silence") {
    MaybeChannelID which_channel = {};
    doc::Note random_note{60};
    doc::Document document{one_note_document(which_channel, random_note)};
    CommandQueue no_command;

    std::vector<Amplitude> buffer = run_new_synth(
        document, 48000, 4 * 1024, no_command.begin()
    );
    for (size_t idx = 0; idx < buffer.size(); idx++) {
        Amplitude y = buffer[idx];
        if (y != 0) {
            CAPTURE(idx);
            CHECK(y == 0);
        }
    }
}


TEST_CASE("Test that playing empty documents produces silence") {
    // This test fails, whereas the one above passes. IDK what's wrong.

    MaybeChannelID which_channel = {};
    doc::Note random_note{60};
    doc::Document document{one_note_document(which_channel, random_note)};
    CommandQueue play_commands = play_from_begin();

    std::vector<Amplitude> buffer = run_new_synth(
        document, 48000, 4 * 1024, play_commands.begin()
    );
    for (size_t idx = 0; idx < buffer.size(); idx++) {
        Amplitude y = buffer[idx];
        if (y != 0) {
            CAPTURE(idx);
            CHECK(y == 0);
        }
    }
}

using audio::synth::chip_instance::SAMPLES_PER_S_IDEAL;

TEST_CASE("Test that notes produce sound") {

    Spc700ChannelID which_channel;

    PICK(all_channels(which_channel));

    for (doc::Note note = 36; note.value <= 84; note.value += 6) {
        CAPTURE(note.value);
        doc::Document document{one_note_document(which_channel, note)};
        CommandQueue play_commands = play_from_begin();

        auto driver = Spc700Driver(document.frequency_table);

        std::vector<Amplitude> buffer = run_new_synth(
            document, 48000, 4 * 1024, play_commands.begin()
        );
        constexpr Amplitude THRESHOLD = 0.04f;
        check_signed_amplitude(buffer, THRESHOLD);
    }
}

TEST_CASE("Send random values into AudioInstance and look for assertion errors") {

    doc::Note note{60};
    doc::Document document{one_note_document(Spc700ChannelID::Channel1, note)};
    CommandQueue play_commands = play_from_begin();

    auto driver = Spc700Driver(document.frequency_table);

#define INCREASE(x)  x = (x) * 3 / 2 + 3

    // Blip_Buffer had a minimum sample rate of around 1000 Hz. I've replaced it with
    // libsamplerate, but let's keep 1000 Hz as a minimum sample rate to test.
    for (uint32_t smp_per_s = 1000; smp_per_s <= 250'000; INCREASE(smp_per_s)) {
        // smp_per_s * 0.25 second
        run_new_synth(document, smp_per_s, smp_per_s / 4, play_commands.begin());
    }

    // 44100Hz, zero samples
    run_new_synth(document, 44100, 0, play_commands.begin());

    // 48000Hz, various durations
    for (uint32_t nsamp = 1; nsamp <= 100'000; INCREASE(nsamp)) {
        run_new_synth(document, 48000, nsamp, play_commands.begin());
    }
}

TEST_CASE("Send all note pitches into AudioInstance and look for assertion errors") {
    // 32000Hz, 4000 samples, various note pitches.
    CommandQueue play_commands = play_from_begin();
    for (doc::Chromatic pitch = 0; pitch < doc::CHROMATIC_COUNT; pitch++) {
        doc::Document document{
            one_note_document(Spc700ChannelID::Channel1, {pitch})
        };
        run_new_synth(document, 32000, 1000, play_commands.begin());
    }
}

// # Test how OverallSynth responds to AudioCommand (playback or edit messages).

TEST_CASE("Ensure that restarting playback produces the same output range") {
    // The actual output isn't exactly identical after you send a replay command,
    // because OverallSynth only checks for new commands once per tick,
    // causing the "replay" command to be processed partway through the audio block.
    // As a quick workaround, check for an identical amplitude range.
    using audio::synth::STEREO_NCHAN;

    CommandQueue /*mut*/ play_commands;

    auto synth = audio::synth::OverallSynth(
        STEREO_NCHAN,
        SAMPLES_PER_S_IDEAL,
        one_note_document(Spc700ChannelID::Channel1, {60}),
        play_commands.begin(),
        FAST_RESAMPLER);

    constexpr size_t NSAMP = 1000;
    auto buffer = std::vector<Amplitude>(NSAMP * STEREO_NCHAN);

    Amplitude play_min, play_max;
    {
        // Play audio from start.
        play_commands.push(cmd_queue::PlayFrom{timing::GridAndBeat{0, 0}});
        synth.synthesize_overall(buffer, NSAMP);
        play_min = *std::min_element(buffer.begin(), buffer.end());
        play_max = *std::max_element(buffer.begin(), buffer.end());
    }

    Amplitude replay_min, replay_max;
    {
        // Replay audio from start.
        play_commands.push(cmd_queue::PlayFrom{timing::GridAndBeat{0, 0}});
        synth.synthesize_overall(buffer, NSAMP);
        replay_min = *std::min_element(buffer.begin(), buffer.end());
        replay_max = *std::max_element(buffer.begin(), buffer.end());
    }

    CHECK(play_min == replay_min);
    CHECK(play_max == replay_max);
}

TEST_CASE("Ensure that stopping playback works") {
    using audio::synth::STEREO_NCHAN;

    CommandQueue /*mut*/ play_commands;

    auto synth = audio::synth::OverallSynth(
        STEREO_NCHAN,
        SAMPLES_PER_S_IDEAL,
        one_note_document(Spc700ChannelID::Channel1, {60}),
        play_commands.begin(),
        FAST_RESAMPLER);

    constexpr size_t NSAMP = 1000;
    auto buffer = std::vector<Amplitude>(NSAMP * STEREO_NCHAN);
    {
        // Play audio from start.
        play_commands.push(cmd_queue::PlayFrom{timing::GridAndBeat{0, 0}});
        synth.synthesize_overall(buffer, NSAMP);

        Amplitude min = *std::min_element(buffer.begin(), buffer.end());
        Amplitude max = *std::max_element(buffer.begin(), buffer.end());
        CHECK(min < 0);
        CHECK(max > 0);
    }

    {
        // Stop audio playback.
        play_commands.push(cmd_queue::StopPlayback{});

        // The output doesn't stop immediately after you send a stop command,
        // because OverallSynth only checks for new commands once per tick.
        // So run the synth for a bit first, then check that it's silent afterwards.
        synth.synthesize_overall(buffer, NSAMP);
        synth.synthesize_overall(buffer, NSAMP);

        Amplitude min = *std::min_element(buffer.begin(), buffer.end());
        Amplitude max = *std::max_element(buffer.begin(), buffer.end());
        CHECK(min == 0);
        CHECK(max == 0);
    }
}

static doc::Document sample_idx_document(
    Spc700ChannelID which_channel, SampleIndex sample_idx
) {
    using namespace doc;

    Samples samples;

    Instruments instruments;
    instruments[0] = Instrument {
        .name = "50%",
        .keysplit = {InstrumentPatch {
            .sample_idx = sample_idx,
            .adsr = INFINITE,
        }}
    };

    ChipList chips{ChipKind::Spc700};

    Timeline timeline;

    timeline.push_back([&]() -> TimelineFrame {
        EventList one_note {TimedRowEvent{0, RowEvent{60, 0}}};
        EventList blank {};

        std::vector<TimelineCell> channel_cells;
        for (size_t i = 0; i < enum_count<Spc700ChannelID>; i++) {
            if (which_channel == Spc700ChannelID(i)) {
                channel_cells.push_back(TimelineCell{TimelineBlock::from_events(one_note)});
            } else {
                channel_cells.push_back(TimelineCell{});
            }
        }

        return TimelineFrame {
            .nbeats = 1,
            .chip_channel_cells = {move(channel_cells)},
        };
    }());

    return DocumentCopy {
        .sequencer_options = SequencerOptions{
            .target_tempo = 100,
        },
        .frequency_table = equal_temperament(),
        .accidental_mode = AccidentalMode::Sharp,
        .samples = move(samples),
        .instruments = move(instruments),
        .chips = chips,
        .chip_channel_settings = spc_chip_channel_settings(),
        .timeline = move(timeline),
    };
}

using namespace edit::edit_sample_list;

TEST_CASE("Ensure that editing samples mutes playing notes") {
    using audio::synth::STEREO_NCHAN;

    Spc700ChannelID which_channel;
    PICK(all_channels(which_channel));

    CommandQueue /*mut*/ play_commands;

    // Create a document where instrument 0 uses sample 1.
    auto doc = sample_idx_document(which_channel, 1);
    doc.samples[0] = silence();
    doc.samples[1] = pulse_50();

    auto synth = audio::synth::OverallSynth(
        STEREO_NCHAN,
        SAMPLES_PER_S_IDEAL,
        doc.clone(),
        play_commands.begin(),
        FAST_RESAMPLER);

    constexpr size_t NSAMP = 1000;

    Amplitude orig_min, orig_max;
    auto buffer = std::vector<Amplitude>(NSAMP * STEREO_NCHAN);
    {
        // Play audio from start.
        play_commands.push(cmd_queue::PlayFrom{timing::GridAndBeat{0, 0}});
        synth.synthesize_overall(buffer, NSAMP);

        orig_min = *std::min_element(buffer.begin(), buffer.end());
        orig_max = *std::max_element(buffer.begin(), buffer.end());
    }

    // Delete sample 0. This should stop the playing note and move sample 1
    // over sample 0.
    {
        auto maybe_command = std::get<0>(try_remove_sample(doc, 0));
        release_assert(maybe_command);
        play_commands.push(maybe_command->clone_for_audio(doc));
        maybe_command->apply_swap(doc);
    }
    // Replace sample 1 with a quieter version. (TODO either try_replace_sample
    // or delete sample 1 first)
    {
        auto command = replace_sample(doc, 1, pulse_50_quiet());
        play_commands.push(command->clone_for_audio(doc));
        command->apply_swap(doc);
    }

    {
        // The output doesn't stop immediately,
        // because OverallSynth only checks for new commands once per tick.
        // So run the synth for a bit first, then check that it's silent afterwards.
        synth.synthesize_overall(buffer, NSAMP);
        synth.synthesize_overall(buffer, NSAMP);

        Amplitude min = *std::min_element(buffer.begin(), buffer.end());
        Amplitude max = *std::max_element(buffer.begin(), buffer.end());
        CHECK(min == 0);
        CHECK(max == 0);
    }

    {
        // Run the synth for the rest of 1 second, and make sure it begins
        // playing the new quieter sample.
        uint32_t nsamp = SAMPLES_PER_S_IDEAL - 3 * NSAMP;
        buffer.resize(2 * nsamp);
        synth.synthesize_overall(buffer, nsamp);

        auto target_min = doctest::Approx(orig_min / 2).epsilon(0.001);
        auto target_max = doctest::Approx(orig_max / 2).epsilon(0.001);

        Amplitude min = *std::min_element(buffer.begin(), buffer.end());
        Amplitude max = *std::max_element(buffer.begin(), buffer.end());
        CAPTURE(target_min);
        CAPTURE(min);
        CAPTURE(target_max);
        CAPTURE(max);

        CHECK(min == target_min);
        CHECK(max == target_max);
    }
}

// TODO add RapidCheck for randomized testing?
