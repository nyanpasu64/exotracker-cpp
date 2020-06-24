#include "synth.h"
#include "chip_kinds.h"
#include "util/release_assert.h"

#include <cstddef>  // size_t
#include <stdexcept>  // std::logic_error will be used once I add APU2.
#include <utility>  // std::move

namespace audio {
namespace synth {

using chip_kinds::ChipKind;

OverallSynth::OverallSynth(
    uint32_t stereo_nchan,
    uint32_t smp_per_s,
    doc::Document const & document,
    AudioOptions audio_options
) :
    _stereo_nchan(stereo_nchan),
    _clocks_per_sound_update(audio_options.clocks_per_sound_update),
    _nes_blip(smp_per_s, CLOCKS_PER_S)
{
    // Optional non-owning reference to the previous chip, which may/not be APU1.
    // Passed to APU2.
    // If an APU2 is not immediately preceded by an APU1 (if apu1_maybe == nullptr),
    // this is a malformed document, so throw an exception.
    nes_2a03::BaseApu1Instance * apu1_maybe = nullptr;

    for (ChipIndex chip_index = 0; chip_index < document.chips.size(); chip_index++) {
        ChipKind chip_kind = document.chips[chip_index];

        switch (chip_kind) {
            case ChipKind::Apu1: {
                auto apu1_unique = nes_2a03::make_Apu1Instance(
                    chip_index,
                    _nes_blip,
                    CLOCKS_PER_S,
                    doc::FrequenciesRef{document.frequency_table},
                    _clocks_per_sound_update
                );
                apu1_maybe = apu1_unique.get();
                _chip_instances.push_back(std::move(apu1_unique));
                break;
            }

            case ChipKind::COUNT: break;
        }

        if (chip_kind != ChipKind::Apu1) {
            apu1_maybe = nullptr;
        }
    }

    _events.set_timeout(SynthEvent::Tick, 0);
}

void OverallSynth::synthesize_overall(
    doc::Document const & document,
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
    blip_nsamp_t const nsamp = (blip_nsamp_t) mono_smp_per_block;

    /*
    Blip_Buffer::count_clocks(nsamp) initially clamped nsamp to (..., _capacity].
    Even if I remove that clamp, it runs `nsamp << (BLIP_BUFFER_ACCURACY = 16)`.
    So passing in any nsamp in [2^16, ...) will fail due to integer overflow.
    So if we want to accept large nsamp, we must recompute nclk_to_play after each tick.
    */
    blip_nsamp_t samples_so_far = 0;  // [0, nsamp]

    auto play_time = _play_time.load(std::memory_order_relaxed);

    while (true) {
        release_assert(samples_so_far <= nsamp);

        // If I were to call count_clocks() once outside the loop,
        // the value would be incorrect for large nsamp.
        //
        // count_clocks() is clamped to (..Blip_Buffer.buffer_size_].
        // If I were to remove that clamping, it would overflow at 65536
        // due to `count << BLIP_BUFFER_ACCURACY`.
        // To avoid overflow, repeatedly compute it in the loop.
        ClockT nclk_to_play = (ClockT) _nes_blip.count_clocks(nsamp - samples_so_far);
        _events.set_timeout(SynthEvent::EndOfCallback, nclk_to_play);

        ClockT prev_to_tick = _events.get_time_until(SynthEvent::Tick);
        auto [event_id, prev_to_next] = _events.next_event();
        if (event_id == SynthEvent::EndOfCallback) {
            assert(_nes_blip.count_samples(nclk_to_play) == nsamp - samples_so_far);
        }

        // Synthesize audio (synth's time passes).
        if (prev_to_next > 0) {
            // Actually synthesize audio.
            for (auto & chip : _chip_instances) {
                chip->run_chip_for(prev_to_tick, prev_to_next, _nes_blip, _temp_buffer);
            }

            // Read audio from blip_buffer.
            _nes_blip.end_frame((blip_nclock_t) prev_to_next);

            auto writable_region = output_buffer.subspan(samples_so_far);

            blip_nsamp_t nsamp_returned = _nes_blip.read_samples(
                &writable_region[0],
                (blip_nsamp_t) writable_region.size()
            );

            samples_so_far += nsamp_returned;
        }

        // Handle events (synth's time doesn't pass).
        switch (event_id) {
            // implementation detail
            case SynthEvent::EndOfCallback: {
                release_assert_equal(samples_so_far, nsamp);
                release_assert(_nes_blip.samples_avail() == 0);
                goto end;
            }

            // This is the important one.
            case SynthEvent::Tick: {
                ChipIndex const nchip = (ChipIndex) _chip_instances.size();

                for (ChipIndex chip_index = 0; chip_index < nchip; chip_index++) {
                    auto & chip = *_chip_instances[chip_index];

                    RegisterWriteQueue & register_writes = chip._register_writes;
                    release_assert(register_writes.num_unread() == 0);

                    // Reset register write queue.
                    register_writes.clear();

                    // chip's time passes.
                    /// Current tick (just occurred), not next tick.
                    if (_sequencer_running) {
                        auto chip_time = chip.sequencer_tick(document);

                        chip.driver_tick(document);

                        // Ensure all chip sequencers are running in sync.
                        if (chip_index > 0) {
                            // TODO release_assert?
                            assert(
                                play_time == MaybeSequencerTime{chip_time}
                            );
                        }

                        play_time = MaybeSequencerTime{chip_time};
                    } else {
                        chip.driver_tick(document);
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
    // If sequencer not running, overwrite timestamp with none.
    // Otherwise overwrite with latest time.
    _play_time.store(play_time, std::memory_order_seq_cst);
}

// end namespaces
}
}
