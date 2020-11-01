#include "nes_2a03_driver.h"
#include "volume_calc_common.h"

#include <algorithm>  // std::clamp
#include <cmath>  // lround
#include <type_traits>  // std::is_move_constructible_v

#ifdef UNITTEST
#include <doctest.h>
#endif

namespace audio::synth::nes_2a03_driver {

using doc::Instrument;

/// given: frequency
/// find: period_reg clamped between [$0, max_register] (for 2a03, $7ff)
static RegisterInt register_quantize(
    FreqDouble cycles__second,
    ClockT clocks__second,
    int samples__cycle,
    int max_register)
{
    auto clocks__sample = clocks__second / (samples__cycle * cycles__second);
    int reg = (int) lround(clocks__sample - 1);
    // Clamps to [lo, hi] inclusive.
    reg = std::clamp(reg, 0, max_register);
    return reg;
}

static TuningOwned make_tuning_table(
    FrequenciesRef frequencies,  // cycle/s
    ClockT clocks__second,  // NES clock rate
    int samples__cycle,  // Varies by pulse vs. tri
    int max_register)
{
    TuningOwned out;
    out.resize(doc::CHROMATIC_COUNT);

    for (size_t i = 0; i < doc::CHROMATIC_COUNT; i++) {
        out[i] = register_quantize(
            frequencies[i], clocks__second, samples__cycle, max_register
        );
    }
    return out;
}

// # Apu1PulseDriver

Apu1PulseDriver::Envelopes::Envelopes()
    : volume(&Instrument::volume, MAX_VOLUME)
    , arpeggio(&Instrument::arpeggio, 0)
    , wave_index(&Instrument::wave_index, 0)
{}

Apu1PulseDriver::Apu1PulseDriver(
    Apu1PulseDriver::PulseNum pulse_num
) noexcept
    : _pulse_num(pulse_num)
    , _base_address(Address(0x4000 + 0x4 * pulse_num))
{}

void Apu1PulseDriver::stop_playback(RegisterWriteQueue &) {
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
    // Backup state.
    auto prev_state = _prev_state;

    *this = Apu1PulseDriver{pulse_num};
    // Initialize state so we know how to turn off sound.
    _prev_state = prev_state;
    // _next_state = silence.
}

#define ENV_FOREACH(ENV, ITER, ...) \
    ENV.foreach([&] (auto & ITER) { __VA_ARGS__; });

void Apu1PulseDriver::tick(
    doc::Document const& document,
    TuningRef tuning_table,
    EventsRef events,
    RegisterWriteQueue & register_writes
) {
    for (doc::RowEvent event : events) {
        if (event.note.has_value()) {
            doc::Note note = *event.note;

            if (note.is_valid_note()) {
                ENV_FOREACH(_envs, iter, iter.note_on(event.instr));
                _prev_note = note;

            } else if (note.is_release()) {
                ENV_FOREACH(_envs, iter, iter.release(document));
            } else if (note.is_cut()) {
                ENV_FOREACH(_envs, iter, iter.note_cut());
            }
        }
        if (event.instr.has_value()) {
            ENV_FOREACH(_envs, iter, iter.switch_instrument(*event.instr))
        }
        if (event.volume.has_value()) {
            _prev_volume = std::clamp(int(*event.volume), 0, MAX_VOLUME);
        }
    }

    // Set chip volume and increment volume envelope.
    _next_state.volume =
        volume_calc::volume_mul_4x4_4(_prev_volume, _envs.volume.next(document));

    // Set chip duty and increment duty envelope.
    _next_state.duty = _envs.wave_index.next(document);

    // Set chip pitch and increment arpeggio envelope.
    _next_state.period_reg = [&] {
        // In 0CC, arpeggios are processed and pitch registers are written to
        // even if volume is 0, but not after a note cut.
        //
        // Changing pitch may write to $4003, which resets phase and creates a click.
        // There is a way to avoid this click: http://forums.nesdev.com/viewtopic.php?t=231
        // I did not implement that method, so I get clicks.
        auto note = _prev_note.value + _envs.arpeggio.next(document);
        note = std::clamp(note, 0, doc::CHROMATIC_COUNT - 1);
        return tuning_table[(size_t) note];
    }();

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

    // Set the length (table) counter to 1 (pulse is muted after it reaches 0).
    _next_state.length = 1;
    // Prevent length counter from being decremented (causing pulse to mute).
    _next_state.length_halt = 1;

    // https://wiki.nesdev.com/w/index.php/APU_Sweep
    // >if the negate flag is false, the shift count is zero, and the current period is at least $400, the target period will be large enough to mute the channel.
    // >to fully disable the sweep unit, a program must turn off enable and turn on negate, such as by writing $08.
    _next_state.bytes[1] = 0x08;


    for (Address byte_idx = 0; byte_idx < Registers::BYTES; byte_idx++) {
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

static_assert(std::is_move_constructible_v<Apu1Driver>, "");

Apu1Driver::Apu1Driver(ClockT clocks_per_sec, FrequenciesRef frequencies)
    : _tuning_table(make_tuning_table(
        frequencies, clocks_per_sec, PULSE_PERIOD, Apu1PulseDriver::MAX_PERIOD
    ))
    , _pulse1_driver{0}
    , _pulse2_driver{1}
{}

// # Apu2TriDriver

Apu2TriDriver::Envelopes::Envelopes()
    : volume{&Instrument::volume, 1}
    , pitch{&Instrument::pitch, 0}
    , arpeggio{&Instrument::arpeggio, 0}
{}

Apu2TriDriver::Apu2TriDriver(ClockT clocks_per_sec, FrequenciesRef frequencies)
    : _tuning_table(make_tuning_table(
        frequencies, clocks_per_sec, TRI_PERIOD, MAX_PERIOD
    ))
{}

Apu2TriDriver::Apu2TriDriver(TuningOwned tuning_table) noexcept
    : _tuning_table(std::move(tuning_table))
{}

void Apu2TriDriver::stop_playback(RegisterWriteQueue &/*mut*/) {
    // sets _next_state = silence.
    // Setting _prev_state is unnecessary because _first_tick_occurred is false
    // and the next tick will overwrite all registers.
    *this = Apu2TriDriver(std::move(_tuning_table));
}

void Apu2TriDriver::tick(
    doc::Document const& document,
    EventsRef events,
    RegisterWriteQueue &/*mut*/ register_writes
) {
    for (doc::RowEvent event : events) {
        if (event.note.has_value()) {
            doc::Note note = *event.note;

            if (note.is_valid_note()) {
                ENV_FOREACH(_envs, iter, iter.note_on(event.instr));
                _prev_note = note;

            } else if (note.is_release()) {
                ENV_FOREACH(_envs, iter, iter.release(document));

            } else if (note.is_cut()) {
                ENV_FOREACH(_envs, iter, iter.note_cut());
            }
        }
        if (event.instr.has_value()) {
            ENV_FOREACH(_envs, iter, iter.switch_instrument(*event.instr))
        }
        if (event.volume.has_value()) {
            _prev_volume = *event.volume != 0;
        }
    }

    // https://wiki.nesdev.com/w/index.php/APU_Triangle

    bool reload_linear_counter = false;

    // Set chip volume and increment volume envelope.
    {
        bool playing = _prev_volume && _envs.volume.next(document);
        if (playing) {
            // Bit 6:0=1111111: Set the linear counter to nonzero to enable tri.
            // Bit 7=1: stop the length (table) counter from ticking
            // and keep writing a nonzero value to the linear counter
            // to keep tri playing.
            _next_state.bytes[0] = 0xff;

            // Reload linear counter to trigger playback.
            // (Why is this necessary?)
            reload_linear_counter = true;

        } else {
            // Bit 6:0=0000000: Set the linear counter to zero to mute tri.
            // Bit 7=1: stop the length (table) counter from ticking
            // and keep writing zero value to the linear counter (meaningless).
            _next_state.bytes[0] = 0x80;
        }
    }

    // ignore _envs.pitch until we implement pitch envelopes.

    // Set chip pitch and increment arpeggio envelope.
    _next_state.period_reg = [&] {
        // In 0CC, arpeggios are processed and pitch registers are written to
        // even if volume is 0, but not after a note cut.
        //
        // Changing pitch may write to $4003, which resets phase and creates a click.
        // There is a way to avoid this click: http://forums.nesdev.com/viewtopic.php?t=231
        // I did not implement that method, so I get clicks.
        auto note = _prev_note.value + _envs.arpeggio.next(document);
        note = std::clamp(note, 0, doc::CHROMATIC_COUNT - 1);
        return _tuning_table[(size_t) note];
    }();

    // Output register writes.
    if (!_first_tick_occurred || _next_state.bytes[0] != _prev_state.bytes[0]) {
        register_writes.push_write({0x4008, Byte(_next_state.bytes[0])});
    }

    // $4009 is unused.
    if (!_first_tick_occurred || _next_state.bytes[2] != _prev_state.bytes[2]) {
        register_writes.push_write({0x400A, Byte(_next_state.bytes[2])});
    }

    // $400B has the side effect of reloading the linear counter.
    // So unconditionally write it if desired.
    if (
        !_first_tick_occurred ||
        reload_linear_counter ||
        _next_state.bytes[3] != _prev_state.bytes[3]
    ) {
        register_writes.push_write({0x400B, Byte(_next_state.bytes[3])});
    }

    _first_tick_occurred = true;
    _prev_state = _next_state;
    return;
}

// # Apu2NoiseDriver

Apu2NoiseDriver::Envelopes::Envelopes()
    : volume(&Instrument::volume, MAX_VOLUME)
    , arpeggio(&Instrument::arpeggio, 0)
    , wave_index(&Instrument::wave_index, 0)
{}

Apu2NoiseDriver::Apu2NoiseDriver() noexcept {
}

void Apu2NoiseDriver::stop_playback(RegisterWriteQueue &/*mut*/) {
    *this = Apu2NoiseDriver();
}

void Apu2NoiseDriver::tick(
    doc::Document const& document,
    EventsRef events,
    RegisterWriteQueue &/*mut*/ register_writes)
{
    for (doc::RowEvent event : events) {
        if (event.note.has_value()) {
            doc::Note note = *event.note;

            if (note.is_valid_note()) {
                ENV_FOREACH(_envs, iter, iter.note_on(event.instr));
                _prev_note = note;

            } else if (note.is_release()) {
                ENV_FOREACH(_envs, iter, iter.release(document));
            } else if (note.is_cut()) {
                ENV_FOREACH(_envs, iter, iter.note_cut());
            }
        }
        if (event.instr.has_value()) {
            ENV_FOREACH(_envs, iter, iter.switch_instrument(*event.instr))
        }
        if (event.volume.has_value()) {
            _prev_volume = std::clamp(int(*event.volume), 0, MAX_VOLUME);
        }
    }

    // Set chip volume and increment volume envelope.
    _next_state.volume =
        volume_calc::volume_mul_4x4_4(_prev_volume, _envs.volume.next(document));

    // Set noise/pitched and increment duty envelope.
    _next_state.pitched = _envs.wave_index.next(document) & 1;

    // Set noise pitch and increment arpeggio envelope.
    _next_state.period_reg =
        ((_prev_note.value + _envs.arpeggio.next(document)) & 0xf) ^ 0xf;

    // https://wiki.nesdev.com/w/index.php/APU_Envelope
    _next_state.const_vol = 1;

    // Set the length (table) counter to 1 (noise is muted after it reaches 0).
    _next_state.length = 1;
    // Prevent length counter from being decremented (causing noise to mute).
    _next_state.length_halt = 1;

    for (Address byte_idx = 0; byte_idx < Registers::BYTES; byte_idx++) {
        if (
            !_first_tick_occurred
            || _next_state.bytes[byte_idx] != _prev_state.bytes[byte_idx]
        ) {
            auto write = RegisterWrite{
                .address = (Address) (0x400C + byte_idx),
                .value = (Byte) (_next_state.bytes[byte_idx])
            };
            register_writes.push_write(write);
        }
    }
    _first_tick_occurred = true;
    _prev_state = _next_state;
    return;
}

// # Apu2DpcmDriver

void Apu2DpcmDriver::set_dmc(
    RegisterWriteQueue &/*mut*/ register_writes, Byte amplitude
) {
    register_writes.push_write({0x4011, amplitude});
}

void Apu2DpcmDriver::stop_playback(RegisterWriteQueue &/*mut*/ register_writes) {
    register_writes.push_write({0x4011, 0});
}

void Apu2DpcmDriver::tick(
    doc::Document const& document,
    EventsRef events,
    RegisterWriteQueue &/*mut*/ register_writes)
{
    for (doc::RowEvent event : events) {
        if (event.note.has_value()) {
            doc::Note note = *event.note;

            if (note.is_cut()) {
                set_dmc(register_writes, 0);
            }
        }
        if (event.volume.has_value()) {
            set_dmc(register_writes, *event.volume);
        }
    }
}


// # Apu2Driver

void Apu2Driver::stop_playback(RegisterWriteQueue &/*mut*/ register_writes) {
    _tri_driver.stop_playback(register_writes);
    _noise_driver.stop_playback(register_writes);
    _dpcm_driver.stop_playback(register_writes);
}

void Apu2Driver::driver_tick(
    doc::Document const& document,
    EnumMap<ChannelID, EventsRef> const& channel_events,
    RegisterWriteQueue &/*mut*/ register_writes)
{
    _tri_driver.tick(document, channel_events[ChannelID::Tri], register_writes);
    _noise_driver.tick(document, channel_events[ChannelID::Noise], register_writes);
    _dpcm_driver.tick(document, channel_events[ChannelID::Dpcm], register_writes);
}

#ifdef UNITTEST
TEST_CASE("Ensure make_tuning_table() produces only valid register values.") {
    // 0CC-FamiTracker uses 1789773 as the master clock rate.
    // Given A440, it writes $0FD to the APU1 pulse period.
    CHECK(
        register_quantize(
            440, 1789773, Apu1Driver::PULSE_PERIOD, Apu1PulseDriver::MAX_PERIOD
        ) == 0x0FD
    );

    FrequenciesOwned freq;
    freq.resize(doc::CHROMATIC_COUNT);
    std::fill(freq.begin(), freq.end(), 1);
    freq[1] = 1'000;
    freq[2] = 1'000'000;
    freq[3] = 1'000'000'000;

    TuningOwned tuning_table =
        make_tuning_table(
            freq, 1789773, Apu1Driver::PULSE_PERIOD, Apu1PulseDriver::MAX_PERIOD
        );
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
