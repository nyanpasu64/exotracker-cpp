#include "chip_instance_common.h"
#include "util/release_assert.h"


namespace audio::synth::chip_instance {

namespace {

enum class ChipEvent {
    RegWrite,
    EndOfTick,  // Should never be popped. Its value is used to ensure all RegWrite complete during the current tick.
    COUNT,
};

}


void ChipInstance::flush_register_writes() {
    // You should not tick the driver before the previous tick finishes playing.
    release_assert_equal(_register_writes.num_unread(), 0);
    _register_writes.clear();
}

NsampWritten ChipInstance::run_chip_for(
    ClockT num_clocks,
    WriteTo write_to)
{
    // The function must end before/equal to the next tick.
    EventQueue<ChipEvent> chip_events;
    chip_events.set_timeout(ChipEvent::EndOfTick, num_clocks);

    /// Schedule the next register write command (from _register_writes)
    /// on chip_events (a timing system).
    auto fetch_next_reg = [](
        RegisterWriteQueue & _register_writes,
        EventQueue<ChipEvent> & chip_events
    ) {
        if (auto * next_reg = _register_writes.peek_mut()) {
            // Truncate all timestamps so they don't overflow current tick
            // (mimic how FamiTracker does it).
            next_reg->time_before = std::min(
                next_reg->time_before, chip_events.get_time_until(ChipEvent::EndOfTick)
            );
            chip_events.set_timeout(ChipEvent::RegWrite, next_reg->time_before);
        }
    };
    fetch_next_reg(_register_writes, chip_events);

    gsl::span buffer_tail = write_to;

    // Time elapsed (in clocks).

    // Total samples written to per-chip mixing buffer.
    // If writing to _nes_blip, should end at 0. Otherwise should end at nsamp_expected.
    NsampT nsamp_total = 0;

    while (true) {
        // Find the time until the next event (either "time of register write" or "end of tick").
        auto ev = chip_events.next_event();

        if (ev.clk_elapsed > 0) {
            // Update the list of register write commands, with the time elapsed.
            if (auto * next_reg = _register_writes.peek_mut()) {
                next_reg->time_before -= ev.clk_elapsed;
            }

            // Run the synth to generate audio (time passes).
            NsampT nsamp_from_call = synth_run_clocks(ev.clk_elapsed, buffer_tail);

            nsamp_total += nsamp_from_call;
            buffer_tail = buffer_tail.subspan(nsamp_from_call * STEREO_NCHAN);
        }

        // Write registers (time doesn't pass).
        ChipEvent id = ev.event_id;
        switch (id) {
        case ChipEvent::RegWrite: {
            // This assumes that there is a register write command,
            // and asserts that its time_before == 0.
            synth_write_reg(_register_writes.pop());
            fetch_next_reg(_register_writes, chip_events);
            break;
        }
        case ChipEvent::EndOfTick: {
            goto end_while;
        }
        case ChipEvent::COUNT: break;
        }
    }
    end_while:
    return nsamp_total;
}

// end namespace
}
