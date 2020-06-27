#include "nes_2a03_driver.h"

#include <algorithm>  // std::clamp
#include <cmath>  // lround

#ifdef UNITTEST
#include <doctest.h>
#endif

namespace audio::synth::nes_2a03_driver {

/// given: frequency
/// find: period_reg clamped between [$0, $7FF]
static RegisterInt register_quantize(
    FreqDouble const cycles_per_second, ClockT const clocks_per_second
) {
    /*
    period [clock/cycle] = 16 * (period_reg + 1)
    period [clock/s]/[cycle/s] = clocks_per_second / frequency

    16 * (period_reg + 1) = clocks_per_second / frequency   </16, -1>
    period_reg = clocks_per_second / frequency / 16, -clamped 1
    */

    int reg = (int) lround(clocks_per_second / (cycles_per_second * 16) - 1);
    // Clamps to [lo, hi] inclusive.
    reg = std::clamp(reg, 0, Apu1PulseDriver::MAX_PERIOD);
    return reg;
}

#ifdef UNITTEST
TEST_CASE("Ensure register_quantize() produces correct values.") {
    // 0CC-FamiTracker uses 1789773 as the master clock rate.
    // Given A440, it writes $0FD to the APU1 pulse period.
    CHECK(register_quantize(440, 1789773) == 0x0FD);
}
#endif

TuningOwned make_tuning_table(
    FrequenciesRef const frequencies,  // cycle/s
    ClockT const clocks_per_second  // clock/s
) {
    TuningOwned out;
    for (size_t i = 0; i < doc::CHROMATIC_COUNT; i++) {
        out[i] = register_quantize(frequencies[i], clocks_per_second);
    }
    return out;
}

void Apu1PulseDriver::stop_note([[maybe_unused]] RegisterWriteQueue & register_writes) {
    /*
    When we stop all notes, we want to reset all mutable state
    (except for cached register contents).
    To avoid forgetting to overwrite some fields,
    I decided to overwrite *this with a new Apu1PulseDriver.
    However, Apu1PulseDriver and EnvelopeIterator have const fields,
    which disable the assignment operator.

    Logically, the issue is that we mix mutable state (to be assigned)
    and immutable constants (makes no sense to reassign).

    Options:

    - Transform const fields into template non-type arguments.
      So each combination of const fields will generate a new copy of each method.
      Bloats codegen by 2-3 times, and a poor fit for N163
      (which has a dynamic channel count).
    - Custom assignment operator that ignores const fields or asserts they match.
    - Mark all fields as non-const.
    - delete this and placement new. Ignores constness entirely.

    I decided to "mark all fields as non-const".

    Rust lacks const fields.
    I wish it had them, but it might break std::mem::replace and assignment via &mut.
    */

    // Backup parameters.
    auto pulse_num = _pulse_num;
    auto tuning_table = _tuning_table;
    // Backup state.
    auto prev_state = _prev_state;

    *this = Apu1PulseDriver{pulse_num, tuning_table};
    // Initialize state so we know how to turn off sound.
    _prev_state = prev_state;
    // _next_state = silence.
}

void Apu1PulseDriver::tick(
    doc::Document const & document,
    sequencer::EventsRef events,
    RegisterWriteQueue & register_writes
) {
    for (doc::RowEvent event : events) {
        if (event.note.has_value()) {
            doc::Note note = *event.note;

            if (note.is_valid_note()) {
                #define NOTE_ON(iter)  iter.note_on(event.instr)
                Apu1PulseDriver_FOREACH(NOTE_ON)
                _prev_note = note;

            } else if (note.is_release()) {
                #define RELEASE(iter)  iter.release(document)
                Apu1PulseDriver_FOREACH(RELEASE)

            } else if (note.is_cut()) {
                #define NOTE_CUT(iter)  iter.note_cut()
                Apu1PulseDriver_FOREACH(NOTE_CUT)
            }
        } else if (event.instr.has_value()) {
            #define SWITCH_INSTRUMENT(iter)  iter.switch_instrument(*event.instr);
            Apu1PulseDriver_FOREACH(SWITCH_INSTRUMENT)
        }
    }

    _next_state.volume = _volume_iter.next(document);
    _next_state.duty = _wave_index_iter.next(document);

    if (_next_state.volume) {
        // Changing pitch may write to $4003, which resets phase and creates a click.
        // There is a way to avoid this click: http://forums.nesdev.com/viewtopic.php?t=231
        // I did not implement that method, so I get clicks.
        auto note = _prev_note.value + _arpeggio_iter.next(document);
        _next_state.period_reg = _tuning_table[
            (size_t) std::clamp(note, 0, doc::CHROMATIC_COUNT - 1)
        ];
    }


    /*
    - https://wiki.nesdev.com/w/index.php/APU#Pulse_.28.244000-4007.29
    - https://wiki.nesdev.com/w/index.php/APU_Pulse
    */

    // https://wiki.nesdev.com/w/index.php/APU_Envelope
    // const_vol could be renamed disable_env or deactivate_env.
    _next_state.const_vol = 1;

    // https://wiki.nesdev.com/w/index.php/APU_Length_Counter
    // Length counter is enabled based on $4015, length_halt, AND length.
    // But length_halt=1 also enables envelope looping.

    // Written by FamiTracker's .nsf driver, but not necessary.
    // _next_state.length_halt = 1;
    // _next_state.length = 1;

    // https://wiki.nesdev.com/w/index.php/APU_Sweep
    // >if the negate flag is false, the shift count is zero, and the current period is at least $400, the target period will be large enough to mute the channel.
    // >to fully disable the sweep unit, a program must turn off enable and turn on negate, such as by writing $08.
    _next_state.bytes[1] = 0x08;


    for (Address byte_idx = 0; byte_idx < Apu1Reg::BYTES; byte_idx++) {
        if (
            !_first_tick_occurred
            || _next_state.bytes[byte_idx] != _prev_state.bytes[byte_idx]
        ) {
            auto write = RegisterWrite{
                .address = (Address) (_base_address + byte_idx),
                .value = (Byte) (_next_state.bytes[byte_idx])
            };
            register_writes.push_write(write);
        }
    }
    _first_tick_occurred = true;
    _prev_state = _next_state;
    return;
}


#ifdef UNITTEST
TEST_CASE("Ensure make_tuning_table() produces only valid register values.") {
    // 0CC-FamiTracker uses 1789773 as the master clock rate.
    // Given A440, it writes $0FD to the APU1 pulse period.
    CHECK(register_quantize(440, 1789773) == 0x0FD);

    FrequenciesOwned freq;
    std::fill(freq.begin(), freq.end(), 1);
    freq[1] = 1'000;
    freq[2] = 1'000'000;
    freq[3] = 1'000'000'000;

    TuningOwned tuning_table = make_tuning_table(freq, 1789773);
    // 2A03 has 11-bit tuning registers.
    for (RegisterInt reg : tuning_table) {
        CHECK(0 <= reg);
        CHECK(reg < (1 << 11));
    }

    // Ensure the clamping edge cases are correct.
    CHECK(tuning_table[0] == (1 << 11) - 1);
    CHECK(tuning_table[3] == 0);
}
#endif

// namespace
}
