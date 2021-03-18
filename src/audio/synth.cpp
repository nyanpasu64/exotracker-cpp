#include "synth.h"
#include "chip_kinds.h"
#include "edit/modified.h"
#include "util/release_assert.h"

#include <fmt/core.h>

#include <cstddef>  // size_t
#include <optional>
#include <utility>  // std::move

namespace audio {
namespace synth {

using chip_kinds::ChipKind;

OverallSynth::OverallSynth(
    uint32_t stereo_nchan,
    uint32_t smp_per_s,
    doc::Document document_moved_from,
    AudioCommand * stub_command,
    AudioOptions audio_options
)
    : _stereo_nchan(stereo_nchan)
    , _document(std::move(document_moved_from))
    , _clocks_per_sound_update(audio_options.clocks_per_sound_update)
{
    // Constructor runs on GUI thread. Fields later be read on audio thread.
    _maybe_seq_time.store(MaybeSequencerTime{}, std::memory_order_relaxed);
    _seen_command.store(stub_command, std::memory_order_relaxed);

    // Thread creation will act as a memory barrier, so we don't need a fence.

    for (ChipIndex chip_index = 0; chip_index < _document.chips.size(); chip_index++) {
        ChipKind chip_kind = _document.chips[chip_index];

        switch (chip_kind) {
            case ChipKind::Spc700: {
                // Possibly more efficient than push_back,
                // and silences false error in IDE.
//                _chip_instances.emplace_back(spc700::make_Spc700Instance());
                break;
            }

            default:
                throw std::logic_error(fmt::format(
                    "OverallSynth() unhandled chip_kind {}", (int) chip_kind
                ));
        }
    }

    _events.set_timeout(SynthEvent::Tick, 0);
}

using edit::ModifiedInt;
using edit::ModifiedFlags;

template<typename T>
T unimplemented() {
    throw "up";
}

void OverallSynth::synthesize_overall(
    gsl::span<Amplitude> output_buffer,
    size_t const mono_smp_per_block
) {
    // Stereo support will be added at a later date.
    release_assert(output_buffer.size() == mono_smp_per_block);

    // In all likelihood this function will not work if stereo_nchan != 1.
    // If I ever add a stereo console (SNES, maybe SN76489 like sneventracker),
    // the code may be fixed (for example using libgme-mpyne Multi_Buffer).

    // GSL uses std::ptrdiff_t (not size_t) for sizes and indexes.
    // Compilers may define ::ptrdiff_t in global namespace. https://github.com/RobotLocomotion/drake/issues/2374
    NsampT const nsamp = (NsampT) mono_smp_per_block;

    /*
    Blip_Buffer::count_clocks(nsamp) initially clamped nsamp to (..., _capacity].
    Even if I remove that clamp, it runs `nsamp << (BLIP_BUFFER_ACCURACY = 16)`.
    So passing in any nsamp in [2^16, ...) will fail due to integer overflow.
    So if we want to accept large nsamp, we must recompute nclk_to_play after each tick.
    */
    NsampT samples_so_far = 0;  // [0, nsamp]

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

    AudioCommand * const orig_cmd = _seen_command.load(std::memory_order_relaxed);
    AudioCommand * cmd = orig_cmd;

    /// Handle all commands we haven't seen yet.
    auto handle_commands = [this, &cmd, &seq_time] () {
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
                    chip->stop_playback();
                    chip->seek(_document, play_from->time);
                }

                // Begin playback (start ticking sequencers).
                _sequencer_running = true;
                // If _sequencer_running == true, SynthEvent::Tick unconditionally
                // overwrites seq_time after calling handle_commands().
            } else
            if (std::get_if<cmd_queue::StopPlayback>(msg)) {
                // Stop active notes.
                for (auto & chip : _chip_instances) {
                    chip->stop_playback();
                }

                // Stop ticking sequencers.
                _sequencer_running = false;
                seq_time = std::nullopt;
            } else
            if (auto edit_ptr = std::get_if<cmd_queue::EditBox>(msg)) {
                // Edit synth's copy of the document.
                auto & edit = **edit_ptr;

                // It's okay to apply edits mid-tick,
                // since _document is only examined by the sequencer and driver,
                // not the hardware synth.
                edit.apply_swap(_document);

                // If not _sequencer_running, edits don't matter. upon playback, we'll seek.
                if (!_sequencer_running) {
                    continue;
                }

                auto modified = edit.modified();
                total_modified |= modified;
            }
        }

        if (total_modified & ModifiedFlags::Tempo) {
            for (auto & chip : _chip_instances) {
                chip->tempo_changed(_document);
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
    };

    // TODO Instrument/tuning edits might invalidate driver or cause OOB reads.

    while (true) {
        release_assert(samples_so_far <= nsamp);

        // If I were to call count_clocks() once outside the loop,
        // the value would be incorrect for large nsamp.
        //
        // count_clocks() is clamped to (..Blip_Buffer.buffer_size_].
        // If I were to remove that clamping, it would overflow at 65536
        // due to `count << BLIP_BUFFER_ACCURACY`.
        // To avoid overflow, repeatedly compute it in the loop.
        ClockT nclk_to_play = unimplemented<ClockT>();
        _events.set_timeout(SynthEvent::EndOfCallback, nclk_to_play);

        ClockT prev_to_tick = _events.get_time_until(SynthEvent::Tick);
        auto [event_id, prev_to_next] = _events.next_event();
        if (event_id == SynthEvent::EndOfCallback) {
            assert(unimplemented<NsampT>() == nsamp - samples_so_far);
        }

        // Synthesize audio (synth's time passes).
        if (prev_to_next > 0) {
            // Actually synthesize audio.
            for (auto & chip : _chip_instances) {
                chip->run_chip_for(prev_to_tick, prev_to_next, {});
            }
            NsampT nsamp_returned = unimplemented<NsampT>();
            samples_so_far += nsamp_returned;
        }

        // Handle events (synth's time doesn't pass).
        switch (event_id) {
            // implementation detail
            case SynthEvent::EndOfCallback: {
                release_assert_equal(samples_so_far, nsamp);
                goto end;
            }

            // This is the important one.
            case SynthEvent::Tick: {
                // Make sure all register writes from the previous frame
                // have been processed by the synth.
                // Set both read and write pointers to 0,
                // so RegisterWriteQueue won't reject further writes.
                for (auto & chip : _chip_instances) {
                    chip->flush_register_writes();
                }

                // We only stop or start playback at the beginning of a tick.
                // May write to registers.
                handle_commands();

                ChipIndex const nchip = (ChipIndex) _chip_instances.size();

                for (ChipIndex chip_index = 0; chip_index < nchip; chip_index++) {
                    auto & chip = *_chip_instances[chip_index];

                    // chip's time passes.
                    /// Current tick (just occurred), not next tick.
                    if (_sequencer_running) {
                        auto chip_time = chip.sequencer_driver_tick(_document);

                        // Ensure all chip sequencers are running in sync.
                        if (chip_index > 0) {
                            // TODO release_assert?
                            assert(seq_time == chip_time);
                        }

                        seq_time = chip_time;
                    } else {
                        chip.driver_tick(_document);
                    }
                }

                // Schedule next tick.
                _events.set_timeout(SynthEvent::Tick, _clocks_per_tick);
                break;
            }

            case SynthEvent::COUNT: break;
        }
    }

    end:

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
}

// end namespaces
}
}
