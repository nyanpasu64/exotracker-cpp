#include "synth.h"
#include "util/macros.h"

#include <cassert>
#include <cstddef>
#include <stdexcept>

namespace audio {
namespace synth {

OverallSynth::OverallSynth(int stereo_nchan, int smp_per_s) :
    _stereo_nchan(stereo_nchan),
    _nes_blip(smp_per_s, CPU_CLK_PER_S)
{
    auto apu1_unique = nes_2a03::make_Apu1Instance(_nes_blip);
//    auto & apu1 = *apu1_unique;
    _chip_instances.push_back(std::move(apu1_unique));

    _events.set_timeout(SynthEvent::Tick, 0);
}

void OverallSynth::synthesize_overall(
    gsl::span<Amplitude> output_buffer, size_t const mono_smp_per_block
) {

    // In all likelihood this function will not work if stereo_nchan != 1.
    // If I ever add a stereo console (SNES, maybe SN76489 like sneventracker),
    // the code may be fixed (for example using libgme-mpyne Multi_Buffer).

    // GSL uses std::ptrdiff_t (not size_t) for sizes and indexes.
    // Compilers may define ::ptrdiff_t in global namespace. https://github.com/RobotLocomotion/drake/issues/2374
    blip_nsamp_t const nsamp = (blip_nsamp_t) mono_smp_per_block;

    ClockT nclk_to_play = (ClockT) _nes_blip.count_clocks(nsamp);
    _events.set_timeout(SynthEvent::EndOfCallback, nclk_to_play);

    blip_nsamp_t samples_so_far = 0;  // [0, nsamp]

    while (true) {
        ClockT prev_to_tick = _events.get_time_until(SynthEvent::Tick);
        auto [event_id, prev_to_next] = _events.next_event();

        // Synthesize audio (synth's time passes).
        {
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
            assert(samples_so_far <= nsamp);
        }

        // Handle events (synth's time doesn't pass).
        switch (event_id) {
            // implementation detail
            case SynthEvent::EndOfCallback: {
                release_assert(samples_so_far == nsamp);
                release_assert(_nes_blip.samples_avail() == 0);
                return;
            }

            // This is the important one.
            case SynthEvent::Tick: {
                // Reset register write queue.
                for (auto & chip : _chip_instances) {
                    RegisterWriteQueue & register_writes = chip->_register_writes;
                    release_assert(register_writes.num_unread() == 0);
                    register_writes.clear();

                    // chip's time passes.
                    chip->driver_tick();
                }

                _events.set_timeout(SynthEvent::Tick, _clocks_per_tick);
                break;
            }

            case SynthEvent::COUNT: break;
        }
    }
}

// end namespaces
}
}
