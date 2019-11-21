#include "synth_common.h"


namespace audio::synth {

namespace {

enum class ChipEvent {
    RegWrite,
    PauseCallback,  // pause register writes.
    EndOfTick,  // Should never be popped. Its value is used to ensure all RegWrite complete during the current tick.
    COUNT,
};

using SampleT = blip_nsamp_t;
}

void ChipInstance::run_chip_for(
    ClockT prev_to_tick,
    ClockT prev_to_next,
    Blip_Buffer & nes_blip,
    gsl::span<Amplitude> temp_buffer
) {

    // The function must end before/equal to the next tick.
    assert(prev_to_next <= prev_to_tick);
    prev_to_next = std::min(prev_to_next, prev_to_tick);

    EventQueue<ChipEvent> reg_q;
    reg_q.set_timeout(ChipEvent::PauseCallback, prev_to_next);
    reg_q.set_timeout(ChipEvent::EndOfTick, prev_to_tick);

    // sometimes i feel like i'm cargo-culting rust's
    // "pass mutable references as parameters,
    // don't make the closure hold onto them" rule in c++
    auto fetch_next_reg = [](
        RegisterWriteQueue & _register_writes,
        EventQueue<ChipEvent> & reg_q
    ) {
        if (auto * next_reg = _register_writes.peek_mut()) {
            // Truncate all timestamps so they don't overflow current tick
            // (mimic how FamiTracker does it).
            next_reg->time_before = std::min(
                next_reg->time_before, reg_q.get_time_until(ChipEvent::EndOfTick)
            );
            reg_q.set_timeout(ChipEvent::RegWrite, next_reg->time_before);
        }
    };
    fetch_next_reg(_register_writes, reg_q);

    gsl::span buffer_tail = temp_buffer;

    // Time elapsed (in clocks).
    ClockT nclock_elapsed = 0;

    // Total samples written to per-chip mixing buffer.
    // If writing to _nes_blip, should end at 0. Otherwise should end at nsamp_expected.
    SampleT nsamp_total = 0;

    // Total samples we should receive from run_chip_for().
    SampleT const nsamp_expected = nes_blip.count_samples((blip_nclock_t) prev_to_next);

    while (true) {
        auto ev = reg_q.next_event();

        // Run the synth to generate audio (time passes).
        if (ev.clk_elapsed > 0) {
            if (auto * next_reg = _register_writes.peek_mut()) {
                next_reg->time_before -= ev.clk_elapsed;
            }

            SampleT nsamp_from_call = synth_run_clocks(
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
            synth_write_memory(_register_writes.pop());
            fetch_next_reg(_register_writes, reg_q);
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
            goto end_while;
        }
        case ChipEvent::COUNT: break;
        }
    }
    end_while:

    if (nsamp_total > 0) {
        assert(nsamp_total == nsamp_expected);
        nes_blip.mix_samples(&temp_buffer[0], nsamp_total);
    }
}

// end namespace
}
