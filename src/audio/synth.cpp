#include "synth.h"
#include "synth/spc700.h"
#include "tempo_calc.h"
#include "chip_kinds.h"
#include "edit/modified.h"
#include "util/release_assert.h"

#include <stdexcept>
#include <fmt/core.h>

#include <cmath>  // round
#include <cstddef>  // size_t
#include <optional>
#include <utility>  // std::move

namespace audio::synth {

using namespace chip_instance;
using chip_kinds::ChipKind;

/// Maximum number of sample frames the SNES emulator can generate in one tick.
/// This amounts to nearly 1/3 of a second, which is absurdly high
/// considering timer rates are generally in the 100s of Hz
/// and the longest timer period (~8000/256 Hz) is only ~32 ms or 1024 sample frames.
constexpr size_t MAX_SNES_BLOCK_SIZE = 10'000;

//#define DONT_RESAMPLE

#ifdef DONT_RESAMPLE
constexpr uint32_t OVERSAMPLING_FACTOR = 1;
#else
constexpr uint32_t OVERSAMPLING_FACTOR = 4;
#endif

SpcResampler::SpcResampler(
    uint32_t stereo_nchan, uint32_t smp_per_s, AudioOptions const& audio_options
)
    : _stereo_nchan(stereo_nchan)
    , _output_smp_per_s(smp_per_s)
    , _resampler_args(SRC_DATA {
        .data_in = nullptr,
        .data_out = nullptr,
        .input_frames = 0,
        .output_frames = 0,
        .input_frames_used = 0, // ignored and overwritten by every src_process() call.
        .output_frames_gen = 0, // ignored and overwritten by every src_process() call.
        .end_of_input = 0,
        .src_ratio = _output_smp_per_s / (SAMPLES_PER_S_IDEAL * OVERSAMPLING_FACTOR),
    })
{
    int resampler_mode = audio_options.resampler_quality;

    #ifdef DONT_RESAMPLE
    _resampler_args.src_ratio = 1.f;
    resampler_mode = SRC_ZERO_ORDER_HOLD;
    #endif

    int error;
    _resampler = src_new(resampler_mode, (int) stereo_nchan, &error);
    if (error) {
        throw std::runtime_error(fmt::format(
            "Failed to create resampler, src_new() error {}", error
        ));
    }
}

SpcResampler::~SpcResampler() {
    src_delete(_resampler);
}

template<typename Fn>
void SpcResampler::resample(Fn generate_input, gsl::span<float> out) {
    _resampler_args.data_out = out.data();
    _resampler_args.output_frames = long(out.size() / _stereo_nchan);

    float * out_end = out.data() + out.size();

    int error = 0;

    while (!_resampler_args.end_of_input && _resampler_args.data_out < out_end) {
        if (_resampler_args.input_frames == 0) {
            // The returned memory *must* have a long lifetime,
            // since it will be used across multiple resample() calls.
            _input = generate_input();

            _resampler_args.data_in = _input.data();
            _resampler_args.input_frames = long(_input.size() / _stereo_nchan);
        }

        error = src_process(_resampler, &_resampler_args);
        if (error) {
            throw std::runtime_error(fmt::format(
                "Failed to run resampler, src_process() error {}", error
            ));
        }

        _resampler_args.data_in +=
            (size_t) (_resampler_args.input_frames_used) * _stereo_nchan;
        _resampler_args.input_frames -= _resampler_args.input_frames_used;

        _resampler_args.data_out +=
            (size_t) (_resampler_args.output_frames_gen) * _stereo_nchan;
        _resampler_args.output_frames -= _resampler_args.output_frames_gen;

        release_assert(_resampler_args.data_in <= _input.data() + _input.size());
        release_assert(_resampler_args.input_frames >= 0);

        release_assert(_resampler_args.data_out <= out_end);
        release_assert(_resampler_args.output_frames >= 0);
    }

    // it stopped for some reason or another, this shouldn't happen, generate silence i guess?
    // if the song stops, sequencers should stop triggering, but driver ticks should continue,
    // and audio should never stop being generated.
    assert(!_resampler_args.end_of_input);
    if (_resampler_args.end_of_input) {
        std::fill(_resampler_args.data_out, out_end, 0.f);
    }
}

using tempo_calc::calc_sequencer_rate;
using tempo_calc::calc_clocks_per_timer;

SequencerTiming::SequencerTiming(doc::SequencerOptions const& options)
    : _clocks_per_timer(calc_clocks_per_timer(options.spc_timer_period))
    , _phase_step(calc_sequencer_rate(options))
    , _phase(DEFAULT_SEQUENCER_PHASE)
{}

void SequencerTiming::recompute_tempo(const doc::SequencerOptions & options)
{
    _clocks_per_timer = calc_clocks_per_timer(options.spc_timer_period);
    _phase_step = calc_sequencer_rate(options);
}

void SequencerTiming::play() {
    _running = true;

    // Reset _phase for more deterministic playback.
    _phase = DEFAULT_SEQUENCER_PHASE;
}

void SequencerTiming::stop() {
    _running = false;
    // TODO port back to std::optional.
    _phase = DEFAULT_SEQUENCER_PHASE;
}

TimerEvent SequencerTiming::run_timer() {
    // Halt the timer and do not tick the sequencer if the driver is not running.
    if (!_running) {
        return TimerEvent::RunDriver;
    }

    uint8_t next_phase = _phase + _phase_step;
    // True if the addition overflowed.
    bool sequencer_ticked = next_phase < _phase;
    _phase = next_phase;

    if (sequencer_ticked) {
        return TimerEvent::TickSequencer;
    } else {
        return TimerEvent::RunDriver;
    }
}


OverallSynth::OverallSynth(
    uint32_t stereo_nchan,
    uint32_t smp_per_s,
    doc::Document document_moved_from,
    AudioCommand * stub_command,
    AudioOptions audio_options
)
    : _document(std::move(document_moved_from))
    , _resampler(stereo_nchan, smp_per_s, audio_options)
    , _sequencer_timing(_document.sequencer_options)
{
    release_assert_equal(stereo_nchan, STEREO_NCHAN);

    // Reserve enough space.
    _temp_buf.resize(MAX_SNES_BLOCK_SIZE * stereo_nchan);

    int const max_oversamples = MAX_SNES_BLOCK_SIZE * OVERSAMPLING_FACTOR;
    _resampler_input.resize(max_oversamples * stereo_nchan);

    // Constructor runs on GUI thread. Fields later be read on audio thread.
    _maybe_seq_time.store(MaybeSequencerTime{}, std::memory_order_relaxed);
    _seen_command.store(stub_command, std::memory_order_relaxed);

    // Thread creation will act as a memory barrier, so we don't need a fence.

    for (ChipIndex chip_index = 0; chip_index < _document.chips.size(); chip_index++) {
        ChipKind chip_kind = _document.chips[chip_index];

        switch (chip_kind) {
            case ChipKind::Spc700: {
                auto instance = spc700::make_Spc700Instance(
                   chip_index, SAMPLES_PER_S_IDEAL, _document.frequency_table
                );

                // This results in flush_register_writes() crashing,
                // for reasons I'm not entirely sure about.
                // It doesn't matter because reset_state() calls reload_samples().
                // instance->reload_samples(_document);

                _chip_instances.emplace_back(std::move(instance));
                break;
            }

            default:
                throw std::logic_error(fmt::format(
                    "OverallSynth() unhandled chip_kind {}", (int) chip_kind
                ));
        }
    }
}

using edit::ModifiedInt;
using edit::ModifiedFlags;

void OverallSynth::synthesize_overall(
    gsl::span<Amplitude> output_buffer,
    size_t const mono_smp_per_block)
{
    release_assert_equal(output_buffer.size(), mono_smp_per_block * STEREO_NCHAN);
    _resampler.resample([&]() { return synthesize_tick_oversampled(); }, output_buffer);
}

gsl::span<float> OverallSynth::synthesize_tick_oversampled() {
    // Thread creation will act as a memory barrier, so we don't need a fence.
    // Only the audio thread writes to _maybe_seq_time and _seen_command.

    MaybeSequencerTime const orig_seq_time =
        _maybe_seq_time.load(std::memory_order_relaxed);

    /// The sequencer's current timestamp in the document.
    /// Increases as the sequencer gets ticked. (Each channel's sequencer is expected to stay in sync.)
    ///
    /// The "end of callback" `seq_time` will get exposed to the GUI
    /// once the audio starts (not finishes) playing.
    /// This is a minor timing discrepancy, but not worth fixing.
    MaybeSequencerTime seq_time = orig_seq_time;

    // Make sure all register writes from the previous frame
    // have been processed by the synth.
    // Set both read and write pointers to 0,
    // so RegisterWriteQueue won't reject further writes.
    for (auto & chip : _chip_instances) {
        chip->flush_register_writes();
    }

    AudioCommand * const orig_cmd = _seen_command.load(std::memory_order_relaxed);
    AudioCommand * cmd = orig_cmd;

    /// Handle all commands we haven't seen yet. This may result in register writes.
    {
        ModifiedInt total_modified = 0;

        // Paired with CommandQueue::push() store(release).
        for (
            ; AudioCommand * next = cmd->next.load(std::memory_order_acquire); cmd = next
        ) {
            cmd_queue::MessageBody * msg = &next->msg;

            // Process each command from the GUI.
            if (auto play_from = std::get_if<cmd_queue::PlayFrom>(msg)) {
                // Seek chip sequencers.
                for (auto & chip : _chip_instances) {
                    // chip->stop_playback();
                    chip->reset_state(_document);
                    chip->seek(_document, play_from->time);
                }

                // Begin playback (start ticking sequencers).
                _sequencer_timing.play();
                // If _sequencer_running == true, SynthEvent::Tick unconditionally
                // overwrites seq_time after calling handle_commands().
            } else
            if (std::get_if<cmd_queue::StopPlayback>(msg)) {
                // Stop active notes.
                for (auto & chip : _chip_instances) {
                    chip->stop_playback();
                }

                // Stop playback.
                _sequencer_timing.stop();
                seq_time = std::nullopt;
            } else
            if (auto edit_ptr = std::get_if<cmd_queue::EditBox>(msg)) {
                // Edit synth's copy of the document.
                auto & edit = **edit_ptr;
                edit.apply_swap(_document);

                // We need to respond to mutations (set flags in total_modified)
                // whether or not the sequencer is running.
                //
                // - Tempo-modifying methods must call SequencerTiming::recompute_tempo()
                //   regardless if the sequencer is playing, because the SPC timer rate may change.
                //
                // - ChipInstance sequencer mutation methods do nothing if the sequencer is stopped.
                //
                // - When I get around to implementing note preview and instrument mutation,
                //   the resulting ChipInstance instrument mutation methods
                //   must be called even when stopped.
                auto modified = edit.modified();
                total_modified |= modified;
            }
        }

        // Tempo changes
        if (total_modified & ModifiedFlags::SequencerOptions) {
            _sequencer_timing.recompute_tempo(_document.sequencer_options);
        }
        if (total_modified & ModifiedFlags::TicksPerBeat) {
            for (auto & chip : _chip_instances) {
                chip->ticks_per_beat_changed(_document);
            }
        }

        if (total_modified & ModifiedFlags::TimelineRows) {
            for (auto & chip : _chip_instances) {
                chip->timeline_modified(_document);
            }
            // Invalidates all sequencer state. We do not need to check the other flags.
        } else if (total_modified & ModifiedFlags::Patterns) {
            for (auto & chip : _chip_instances) {
                chip->doc_edited(_document);
            }
        }
    }

    // TODO Instrument/tuning edits might invalidate driver or cause OOB reads.

    ClockT nclk_to_play = _sequencer_timing.clocks_per_timer();

    TimerEvent const action = _sequencer_timing.run_timer();

    ChipIndex const nchip = (ChipIndex) _chip_instances.size();

    // Optionally tick sequencers, then run drivers.
    for (ChipIndex chip_index = 0; chip_index < nchip; chip_index++) {
        auto & chip = *_chip_instances[chip_index];

        switch (action) {
        case TimerEvent::TickSequencer: {
            /// Current tick (just occurred), not next tick.
            auto chip_time = chip.tick_sequencer(_document);

            // Ensure all chip sequencers are running in sync.
            if (chip_index > 0) {
                // TODO release_assert?
                assert(seq_time == chip_time);
            }

            seq_time = chip_time;
            break;
        }
        case TimerEvent::RunDriver:
            chip.run_driver(_document);
            break;
        }
    }

    // Synthesize audio (synth's time passes).
    NsampT nsamp_written = 0;
    for (ChipIndex chip_index = 0; chip_index < nchip; chip_index++) {
        auto & chip = *_chip_instances[chip_index];

        NsampT chip_written = chip.run_chip_for(nclk_to_play, _temp_buf);

        if (chip_index == 0) {
            nsamp_written = chip_written;
            _resampler_input.resize(nsamp_written * STEREO_NCHAN * OVERSAMPLING_FACTOR);
            std::fill(_resampler_input.begin(), _resampler_input.end(), 0.f);
        } else {
            assert(chip_written == nsamp_written);
        }

        for (size_t i = 0; i < chip_written; i++) {
            // Convert data from short to float.
            float in_left = _temp_buf[i * STEREO_NCHAN + 0] / (1.0f * 0x8000);
            float in_right = _temp_buf[i * STEREO_NCHAN + 1] / (1.0f * 0x8000);

            // Perform ZOH upsampling on data.
            for (size_t j = 0; j < OVERSAMPLING_FACTOR; j++) {
                _resampler_input[(i * OVERSAMPLING_FACTOR + j) * STEREO_NCHAN + 0] += in_left;
                _resampler_input[(i * OVERSAMPLING_FACTOR + j) * STEREO_NCHAN + 1] += in_right;
            }
        }
    }

    // TODO filter _resampler_input.

    // Store final time after synthesis completes.
    if (seq_time != orig_seq_time) {
        _maybe_seq_time.store(seq_time, std::memory_order_seq_cst);
    }

    // Store "seen command" after timestamp.
    // This way, if GUI sees we're done with commands, it sees the right time.
    // Paired with seen_command().
    if (cmd != orig_cmd) {
        _seen_command.store(cmd, std::memory_order_release);
    }

    return gsl::span<float>(
        _resampler_input.data(), nsamp_written * OVERSAMPLING_FACTOR * STEREO_NCHAN
    );
}

// end namespace
}
