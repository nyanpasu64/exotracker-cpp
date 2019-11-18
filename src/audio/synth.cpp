#include "synth.h"

#include <cassert>
#include <cstddef>
#include <stdexcept>

namespace audio {
namespace synth {

OverallSynth::OverallSynth(int stereo_nchan, int smp_per_s) :
    _stereo_nchan(stereo_nchan),
    _nes_blip(smp_per_s, CPU_CLK_PER_S)
{
    _chip_active[NesChipID::NesApu1] = true;
    auto apu1_unique = nes_2a03::make_NesApu1Synth(_nes_blip);
    auto & apu1 = *apu1_unique;
    _chip_synths[NesChipID::NesApu1] = std::move(apu1_unique);

    _chip_active[NesChipID::NesApu2] = true;
    _chip_synths[NesChipID::NesApu2] = nes_2a03::make_NesApu2Synth(_nes_blip, apu1);

    for (auto & chip_synth : _chip_synths) {
        assert(chip_synth != nullptr);
    }

    _events.set_timeout(SynthEvent::Tick, 0);
}


#define FOREACH_ENUM(variable, Enum) \
    for (size_t variable = 0; variable < enum_count<Enum>; variable++)

enum class ChipEvent {
    RegWrite,
    PauseCallback,  // pause register writes.
    EndOfTick,  // Should never be popped. Its value is used to ensure all RegWrite complete during the current tick.
    COUNT,
};

void OverallSynth::run_chip_for(
    ClockT prev_to_tick, ClockT prev_to_next, NesChipID Chip_id
) {
    // The function must end before/equal to the next tick.
    assert(prev_to_next <= prev_to_tick);
    prev_to_next = std::min(prev_to_next, prev_to_tick);

    RegisterWriteQueue & register_writes = _chip_register_writes[Chip_id];
    auto & chip = *_chip_synths[Chip_id];

    EventQueue<ChipEvent> reg_q;
    reg_q.set_timeout(ChipEvent::PauseCallback, prev_to_next);
    reg_q.set_timeout(ChipEvent::EndOfTick, prev_to_tick);

    // sometimes i feel like i'm cargo-culting rust's
    // "pass mutable references as parameters,
    // don't make the closure hold onto them" rule in c++
    auto fetch_next_reg = [](
        RegisterWriteQueue & register_writes,
        EventQueue<ChipEvent> & reg_q
    ) {
        if (auto * next_reg = register_writes.peek_mut()) {
            // Truncate all timestamps so they don't overflow current tick
            // (mimic how FamiTracker does it).
            next_reg->time_before = std::min(
                next_reg->time_before, reg_q.get_time_until(ChipEvent::EndOfTick)
            );
            reg_q.set_timeout(ChipEvent::RegWrite, next_reg->time_before);
        }
    };
    fetch_next_reg(register_writes, reg_q);

    gsl::span buffer_tail = _temp_buffer;

    // Time elapsed (in clocks).
    ClockT nclock_elapsed = 0;

    // Total samples written to per-chip mixing buffer.
    // If writing to _nes_blip, should end at 0. Otherwise should end at nsamp_expected.
    SampleT nsamp_total = 0;

    // Total samples we should receive from run_chip_for().
    SampleT const nsamp_expected = _nes_blip.count_samples((blip_nclock_t) prev_to_next);

    while (true) {
        auto ev = reg_q.next_event();

        // Run the synth to generate audio (time passes).
        if (ev.clk_elapsed > 0) {
            if (auto * next_reg = register_writes.peek_mut()) {
                next_reg->time_before -= ev.clk_elapsed;
            }

            SampleT nsamp_from_call = chip.synthesize_chip_clocks(
                nclock_elapsed, ev.clk_elapsed, buffer_tail
            );

            nclock_elapsed += ev.clk_elapsed;
            nsamp_total += nsamp_from_call;
            buffer_tail = buffer_tail.subspan(nsamp_from_call);
        }

        // Write registers (time doesn't pass).
        ChipEvent id = ev.event_id;
        switch (id) {
        case ChipEvent::RegWrite: {
            // pop() would be unsafe or unwrap() in Rust.
            chip.write_memory(register_writes.pop());
            fetch_next_reg(register_writes, reg_q);
            break;
        }
        case ChipEvent::PauseCallback: {
            goto end_while;
        }
        case ChipEvent::EndOfTick: {
            // Should never happen. This function has a precondition:
            // Clk_to_run (PauseCallback) <= Clk_before_tick (EndOfTick).
            // This ensures that PauseCallback will always abort the loop before EndOfTick occurs.
            // And if same time, (int)PauseCallback < (int)EndOfTick.
            assert(false);
            break;
        }
        case ChipEvent::COUNT: break;
        }
    }
    end_while:

    if (nsamp_total > 0) {
        assert(nsamp_total == nsamp_expected);
        _nes_blip.mix_samples(_temp_buffer, nsamp_total);
    }
};

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

        // Synthesize audio (time passes).
        {
            SampleT nsamp_expected = _nes_blip.count_samples((blip_nclock_t) prev_to_next);

            FOREACH_ENUM(chip_int, NesChipID) {
                auto chip_id = NesChipID(chip_int);
                if (!_chip_active[chip_id]) {
                    continue;
                }

                // Should this return anything? No.
                // We tell it how many clocks to run,
                // and don't care whether it writes 0 or N samples.
                run_chip_for(prev_to_tick, prev_to_next, chip_id);
            }

            _nes_blip.end_frame((blip_nclock_t) prev_to_next);

            // i keep getting signedness warnings: https://github.com/Microsoft/GSL/issues/322
            auto writable_region = output_buffer.subspan(samples_so_far);
            SampleT nsamp_returned = _nes_blip.read_samples(
                &writable_region[0],
                (blip_nsamp_t) writable_region.size()
            );
            assert(nsamp_returned == nsamp_expected);
            samples_so_far += nsamp_returned;
        }

        // Handle events (time doesn't pass).
        switch (event_id) {
            // implementation detail
            case SynthEvent::EndOfCallback: {
                assert(samples_so_far == nsamp);
                assert(_nes_blip.samples_avail() == 0);
                return;
            }

            // This is the important one.
            case SynthEvent::Tick: {
                // Reset register write queue.
                FOREACH_ENUM(chip_int, NesChipID) {
                    RegisterWriteQueue & register_writes = _chip_register_writes[chip_int];
                    assert(register_writes.num_unread() == 0);
                    register_writes.clear();
                }

                // Initialize register write queue with next tick of events.
                _music_driver.get_frame_registers(_chip_register_writes);

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
