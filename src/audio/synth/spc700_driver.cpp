#include "spc700_driver.h"
#include "spc700_p.h"
#include "util/release_assert.h"

#include <algorithm>  // std::copy, std::min

namespace audio::synth::spc700_driver {

Spc700Driver::Spc700Driver(NsampT samples_per_sec, doc::FrequenciesRef frequencies) {
}

/// Placeholder fixed address. TODO find a better filling algorithm.
/// Layout: [0x100] four-byte entries, but we don't have to fill in the whole thing.
constexpr size_t SAMPLE_DIR = 0x100;

/// Each sample directory entry is:
/// - 2 bytes (little endian) for sample start address
/// - 2 bytes (little endian) for sample loop address
/// We write raw bytes instead of casting to a struct pointer,
/// due to C++ endian/alignment/strict aliasing issues.
constexpr size_t SAMPLE_DIR_ENTRY_SIZE = 4;

using spc700::SPC_MEMORY_SIZE;

// TODO test this method.
// Issue is, it's not the most test-friendly method, due to debug assertions,
// writing directly to RAM, and the sample-reloading API still being in flux.
void Spc700Driver::reload_samples(
    doc::Document const& document,
    spc700::Spc700Synth & synth,
    RegisterWriteQueue & register_writes)
{
    std::fill(std::begin(_samples_valid), std::end(_samples_valid), false);

    bool samples_found = false;
    // Contains the index of the last sample present.
    size_t last_smp_idx;

    // Loops from MAX_SAMPLES - 1 through 0.
    for (last_smp_idx = doc::MAX_SAMPLES; last_smp_idx--; ) {
        if (document.samples[last_smp_idx]) {
            samples_found = true;
            break;
        }
    }

    if (samples_found) {
        size_t first_unused_slot = last_smp_idx + 1;

        /// The offset in SPC memory to write the next sample to.
        size_t sample_start_addr = SAMPLE_DIR + first_unused_slot * SAMPLE_DIR_ENTRY_SIZE;

        // This loop was *seriously* overengineered... :(
        for (size_t i = 0; i < first_unused_slot; i++) {
            // We can't assert <, because the previously loaded sample
            // might've entirely filled up RAM to the last byte.
            assert(sample_start_addr <= SPC_MEMORY_SIZE);
            // If RAM is entirely filled, break.
            if (sample_start_addr >= SPC_MEMORY_SIZE) {
                break;
            }

            if (!document.samples[i]) continue;
            auto & smp = *document.samples[i];

            // Debug assertions. Samples which violate these properties are probably wrong,
            // but are safe to load anyway (though they won't play right).
            assert(!smp.brr.empty());
            assert(smp.brr.size() < 0x10000);
            assert(smp.brr.size() % 9 == 0);
            assert(smp.loop_byte < smp.brr.size());

            // Every sample must have a positive length. Skip any samples with zero length.
            if (smp.brr.empty()) {
                continue;
            }

            size_t brr_size_clamped = std::min(smp.brr.size(), (size_t) 0x10000);
            size_t sample_end_addr = sample_start_addr + brr_size_clamped;
            if (sample_end_addr > SPC_MEMORY_SIZE) {
                // Sample data overflow. TODO indicate error to user.

                // Continue trying to load later samples, hopefully they're smaller
                // and fit in the remaining space.
                continue;
            }

            size_t sample_loop_addr = sample_start_addr + smp.loop_byte;
            if (sample_loop_addr >= SPC_MEMORY_SIZE) {
                // Corrupted sample, the loop byte >= the BRR size. IDK what to do.

                // Continue trying to load later samples, hopefully they're smaller
                // and fit in the remaining space.
                continue;
            }

            // Write the sample entry.
            size_t sample_entry_addr = SAMPLE_DIR + i * SAMPLE_DIR_ENTRY_SIZE;
            synth._ram_64k[sample_entry_addr + 0] = (uint8_t) sample_start_addr;
            synth._ram_64k[sample_entry_addr + 1] = (uint8_t) (sample_start_addr >> 8);
            synth._ram_64k[sample_entry_addr + 2] = (uint8_t) sample_loop_addr;
            synth._ram_64k[sample_entry_addr + 3] = (uint8_t) (sample_loop_addr >> 8);

            // Write the sample data.
            std::copy(
                smp.brr.begin(),
                smp.brr.begin() + brr_size_clamped,
                &synth._ram_64k[sample_start_addr]);

            sample_start_addr = sample_end_addr;
        }
    }

    // When samples are moved around in RAM, playback must be stopped.
    // TODO hard-cut all notes, don't just trigger release envelopes.
    // Maybe reset the APU and rewrite all active effects?

    // stop_playback(register_writes) is insufficient because I'm planning
    // for it to not hard-cut all channels, only release.
}

void Spc700Driver::stop_playback(RegisterWriteQueue /*mut*/& register_writes) {
}

void Spc700Driver::driver_tick(
    doc::Document const& document,
    EnumMap<ChannelID, EventsRef> const& channel_events,
    RegisterWriteQueue &/*mut*/ register_writes)
{}

}
