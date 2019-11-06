#include "synth.h"

#include <cassert>
#include <cstddef>

namespace audio {
namespace synth {

#define FOREACH(Enum, variable) \
    for (size_t variable = 0; variable < Enum::COUNT; variable++)

void OverallSynth::synthesize_overall(
        gsl::span<Amplitude> output_buffer, size_t const mono_smp_per_block
        ) {

    // In all likelihood this function will not work if stereo_nchan != 1.
    // If I ever add a stereo console (SNES, maybe SN76489 like sneventracker),
    // the code may be fixed (for example using libgme-mpyne Multi_Buffer).

    // GSL uses std::ptrdiff_t (not size_t) for sizes and indexes.
    // Compilers may define ::ptrdiff_t in global namespace. https://github.com/RobotLocomotion/drake/issues/2374
    std::size_t const nsamp = mono_smp_per_block;
    std::ptrdiff_t samples_so_far = 0;  // [0, nsamp]

    /// Writes to output buffer, can be called multiple times (appends after end of previous call)
    /// Should this code be inlined (moving it away from samples_so_far)?
    auto synthesize_all_for = [this, &samples_so_far, nsamp, output_buffer]
            (ClockT Nclk)
    {
        if (Nclk == 0) return;

        // signed long (64-bit on linux64)? size_t (64-bit on x64)? uint32_t?
        // blip_buffer uses blip_long AKA signed int for nsamp.
        using SampleT = blip_nsamp_t;

        SampleT nsamp_expected = _nes_blip.count_samples((blip_nclock_t) Nclk);

        // Synthesize and mix audio from each enabled chip.
        FOREACH(NesChipID, chip_id) {
            if (_chip_active[chip_id]) {

                // Run the chip for a specific number of clocks.
                // Most chips will write to _nes_blip
                // (if they have a Blip_Synth with a mutable aliased reference to Blip_Buffer).
                // The FDS will instead write lowpassed audio to _temp_buffer.
                auto synth_result = _chip_synths[chip_id]->synthesize_chip_clocks(
                            Nclk, _temp_buffer);

                // If the chip outputs audio instead of _nes_blip steps, mix the audio into _nes_blip.
                if (synth_result.wrote_audio) {
                    assert(synth_result.nsamp_returned == nsamp_expected);
                    _nes_blip.mix_samples(_temp_buffer, synth_result.nsamp_returned);
                }
            }
        }

        // After writing all audio to blip_buffer, advance time.
        _nes_blip.end_frame(Nclk);

        // i keep getting signedness warnings: https://github.com/Microsoft/GSL/issues/322
        auto writable_region = output_buffer.subspan(samples_so_far);
        SampleT nsamp_returned =
                _nes_blip.read_samples(&writable_region[0], writable_region.size());
        assert(nsamp_returned == nsamp_expected);
        samples_so_far += nsamp_returned;
    };

    ClockT nclk_to_play = _nes_blip.count_clocks(nsamp);

    // FIXME replace this with interleaved (synthesis, handle event)
    synthesize_all_for(nclk_to_play);
}

// end namespaces
}
}
