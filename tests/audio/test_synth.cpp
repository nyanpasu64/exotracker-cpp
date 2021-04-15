#include "audio/synth.h"
#include "audio/synth/chip_instance_common.h"
#include "audio/synth/spc700_driver.h"
#include "doc.h"
#include "chip_kinds.h"
#include "cmd_queue.h"
#include "timing_common.h"
#include "doc_util/sample_instrs.h"
#include "test_utils/parameterize.h"

#include <fmt/core.h>

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
#include <utility>  // std::move

#include "doctest.h"

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

    timeline.push_back([&]() -> TimelineRow {
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

        return TimelineRow {
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

CommandQueue play_from_begin() {
    CommandQueue out;
    out.push(cmd_queue::PlayFrom{timing::GridAndBeat{0, 0}});
    return out;
}

/// Constructs a new OverallSynth at the specified sampling rate,
/// and runs it for the specified amount of time.
/// Returns the generated audio.
std::vector<Amplitude> run_new_synth(
    doc::Document const & document,
    uint32_t smp_per_s,
    NsampT nsamp,
    AudioCommand * stub_command)
{
    using audio::synth::STEREO_NCHAN;

    CAPTURE(smp_per_s);
    CAPTURE(nsamp);

    // (int stereo_nchan, int smp_per_s, locked_doc::GetDocument &/*'a*/ document)
    audio::synth::OverallSynth synth{
        STEREO_NCHAN, smp_per_s, document.clone(), stub_command, FAST_RESAMPLER
    };

    std::vector<Amplitude> buffer;
    buffer.resize(nsamp * STEREO_NCHAN);
    synth.synthesize_overall(/*mut*/ buffer, nsamp);

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

        auto driver = Spc700Driver(SAMPLES_PER_S_IDEAL, document.frequency_table);

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

    auto driver = Spc700Driver(SAMPLES_PER_S_IDEAL, document.frequency_table);

#define INCREASE(x)  x = (x) * 3 / 2 + 3

    // Setting smp_per_s to small numbers breaks blip_buffer's internal invariants.
    // >assert( length_ == msec ); // ensure length is same as that passed in
    // The largest failing value is 873.
    // Not all values fail. As smp_per_s decreases, it becomes more likely to fail.
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
    for (doc::ChromaticInt pitch = 0; pitch < doc::CHROMATIC_COUNT; pitch++) {
        doc::Document document{
            one_note_document(Spc700ChannelID::Channel1, {pitch})
        };
        run_new_synth(document, 32000, 1000, play_commands.begin());
    }
}

// TODO add RapidCheck for randomized testing?
