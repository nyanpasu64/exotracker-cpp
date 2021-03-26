#include "spc700_driver.h"
#include "spc700_synth.h"
#include "chip_instance_common.h"
#include "doc.h"
#include "util/release_assert.h"

#include <SPC_DSP.h>  // for register enums
#include <gsl/span>

#include <algorithm>  // std::copy, std::min, std::clamp
#include <cmath>

//#define DRIVER_DEBUG

#ifdef DRIVER_DEBUG
#include <fmt/core.h>
#define DEBUG_PRINT(...) fmt::print(stderr, __VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif

namespace audio::synth::spc700_driver {

Spc700ChannelDriver::Spc700ChannelDriver(uint8_t channel_id)
    : _channel_id(channel_id)
{
}

constexpr double CENTS_PER_OCTAVE = 1200.;
using chip_instance::SAMPLES_PER_S_IDEAL;

static uint16_t calc_tuning(
    FrequenciesRef freq_table, doc::SampleTuning const& tuning, doc::Note note
) {
    // At pitch 0x1000, samples are played at the SNES's native rate
    // (1 output sample per input sample, around 32000 Hz).
    double tuning_reg_f = double(tuning.sample_rate) / SAMPLES_PER_S_IDEAL * 0x1000;

    // Increase the pitch by tuning.detune_cents.
    tuning_reg_f *= exp2(double(tuning.detune_cents) / CENTS_PER_OCTAVE);

    // Increase the pitch by the note key relative to the sample's root key.
    release_assert(note.is_valid_note());
    release_assert(doc::Note(tuning.root_key).is_valid_note());

    // Use the tuning table to detune notes. (This allows for custom tuning schemes,
    // though not supporting microtonal music not mapped to the chromatic scale.)
    tuning_reg_f *=
        freq_table[(size_t) note.value] / freq_table[(size_t) tuning.root_key];

    // Pitch registers are played back modulo 0x4000.
    // Clamp out-of-range registers instead of letting them wrap around.
    // (This could be reconfigurable?)
    auto out = (uint16_t) std::round(std::clamp(tuning_reg_f, double(0), double(0x3fff)));

    DEBUG_PRINT("    calc_tuning(): note {} -> pitch register {:04x}\n",
        note.value, out
    );

    return out;
}

static doc::InstrumentPatch const* find_patch(
    gsl::span<doc::InstrumentPatch const> keysplit, doc::ChromaticInt note
) {
    for (doc::InstrumentPatch const& patch : keysplit) {
        if (patch.min_note <= note && note <= patch.max_note_inclusive) {
            return &patch;
        }
    }
    return nullptr;
}

/// Compute the address of per-voice registers, given our current channel number.
static Address calc_voice_reg(size_t channel_id, Address v_reg) {
    auto channel_addr = Address(channel_id << 4);
    release_assert(v_reg <= 0x09);  // the largest SPC_DSP::v_... value.
    return channel_addr + v_reg;
}

/// For some registers, we must wait two full samples worth of clocks
/// to make sure that the S-DSP has seen and processed the register write
/// (see the "every_other_sample" variable).
constexpr ClockT CLOCKS_PER_TWO_SAMPLES = 64;
// TODO do any bad consequences happen if we don't wait 2 samples?
// Is it possible for each tick to be shorter than 2 sample on real hardware?
// If we set a high enough tempo and tick rate, then we may not wait 2 samples per tick,
// and ChipInstance::run_chip_for() will truncate our register write
// to prevent it from overflowing the tick.

Spc700Driver::Spc700Driver(NsampT samples_per_sec, doc::FrequenciesRef frequencies)
    : _channels{
        Spc700ChannelDriver(0),
        Spc700ChannelDriver(1),
        Spc700ChannelDriver(2),
        Spc700ChannelDriver(3),
        Spc700ChannelDriver(4),
        Spc700ChannelDriver(5),
        Spc700ChannelDriver(6),
        Spc700ChannelDriver(7),
    }
    // c++ is... special(ized).
    , _freq_table(frequencies.begin(), frequencies.end())
{
    // TODO, samples_per_sec is ignored.
    // Should calc_tuning() be based off the actual playback frequency,
    // the average frequency (32040 or more), the ideal frequency (32000),
    // or a document-specific tuning?
    // tbh the tuning deviations are small enough to not matter.
    // Should I remove the samples_per_sec parameter?

    assert(_freq_table.size() == frequencies.size());
}

void Spc700Driver::reset_state(
    doc::Document const& document,
    Spc700Synth & synth,
    RegisterWriteQueue & regs)
{
    synth._chip.reset();

    // TODO replace *this with a freshly constructed instance?
    // idk, state management is hard, and we need a principled strategy (idk what yet).

    reload_samples(document, synth, regs);  // writes SAMPLE_DIR to $5D.

    // Initialize registers:
    // Maximize master volume.
    regs.write(SPC_DSP::r_mvoll, 0x7f);
    regs.write(SPC_DSP::r_mvolr, 0x7f);

    // Disable soft reset, unmute amplifier, disable echo writes, set noise frequency to 0.
    // TODO add configurable echo buffer duration, and exclude that space from sample loading.
    regs.write(SPC_DSP::r_flg, 0b001'00000);

    // Mute echo output. TODO add configurable echo volume
    regs.write(SPC_DSP::r_evoll, 0);
    regs.write(SPC_DSP::r_evolr, 0);

    // Disable pitch modulation. TODO add pitch mod toggle
    regs.write(SPC_DSP::r_pmon, 0x00);

    // Disable noise. TODO add noise toggle
    regs.write(SPC_DSP::r_non, 0x00);

    // Disable echo input. TODO add per-channel echo toggle
    regs.write(SPC_DSP::r_eon, 0x00);

    // Disable key-on.
    // If we don't write this, the internal m.new_kon is nonzero by default (value 0xD1),
    // and will trigger key-on on some channels even when we don't process notes.
    regs.write(SPC_DSP::r_kon, 0x00);

    // TODO initialize r_efb, r_esa, r_edl, r_fir + 0x10*n. (r_endx is not useful.)

    for (size_t i = 0; i < enum_count<ChannelID>; i++) {
        // Volume 32 out of [-128..127] is an acceptable default.
        // 64 results in clipping when playing many channels at once.
        regs.write(calc_voice_reg(i, SPC_DSP::v_voll), 0x20);
        regs.write(calc_voice_reg(i, SPC_DSP::v_volr), 0x20);

        // TODO set GAIN (not used yet).
    }
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

using spc700_synth::SPC_MEMORY_SIZE;

// TODO test this method.
// Issue is, it's not the most test-friendly method, due to debug assertions,
// writing directly to RAM, and the sample-reloading API still being in flux.
void Spc700Driver::reload_samples(
    doc::Document const& document,
    Spc700Synth & synth,
    RegisterWriteQueue & regs)
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
                smp.brr.begin() + (ptrdiff_t) brr_size_clamped,
                &synth._ram_64k[sample_start_addr]);

            sample_start_addr = sample_end_addr;
            _samples_valid[i] = true;
        }
    }

    // Set base address.
    regs.write(SPC_DSP::r_dir, SAMPLE_DIR >> 8);

    // When samples are moved around in RAM, playback must be stopped.
    // TODO hard-cut all notes, don't just trigger release envelopes.
    // Maybe reset the APU and rewrite all active effects?

    // stop_playback(regs) is insufficient because I'm planning
    // for it to not hard-cut all channels, only release.
}

void Spc700Driver::stop_playback(RegisterWriteQueue /*mut*/& regs) {
    regs.write(SPC_DSP::r_koff, 0xff);
    regs.wait(CLOCKS_PER_TWO_SAMPLES);
}

void Spc700ChannelDriver::tick(
    doc::Document const& document,
    Spc700Driver const& chip_driver,
    EventsRef events,
    RegisterWriteQueue &/*mut*/ regs,
    Spc700ChipFlags & flags)
{
    auto const channel_flag = uint8_t(1 << _channel_id);

    auto voice_reg8 = [this, &regs](Address v_reg, uint8_t value) {
        auto addr = calc_voice_reg(_channel_id, v_reg);
        regs.write(addr, value);
    };
    auto voice_reg16 = [this, &regs](Address v_reg, uint16_t value) {
        auto addr = calc_voice_reg(_channel_id, v_reg);
        regs.write(addr, (uint8_t) value);
        regs.write(Address(addr + 1), (uint8_t) (value >> 8));
    };

    using Success = bool;
    auto try_play_note = [&](doc::Note note) -> Success {
        // TODO perhaps return pitch || 0, and don't run voice_reg16(),
        // but instead return a pitch directly and let the caller cache it
        // for pitch bends and vibrato.
        // TODO perhaps cache the currently loaded keysplit tuning?! idk if practical

        if (!_prev_instr) {
            DEBUG_PRINT("    cannot play note, no instrument set\n");
            return false;
        }

        if (!document.instruments[*_prev_instr]) {
            DEBUG_PRINT("    cannot play note, instrument {:02x} does not exist\n", *_prev_instr);
            return false;
        }

        auto patch = find_patch(document.instruments[*_prev_instr]->keysplit, note.value);
        if (!patch) {
            DEBUG_PRINT("    cannot play note, instrument {:02x} does not contain note {}\n",
                *_prev_instr, note.value
            );
            return false;
        }

        // Check to see if the sample has been loaded into ARAM or not
        // (due to missing sample or ARAM being full).
        if (!chip_driver._samples_valid[patch->sample_idx]) {
            DEBUG_PRINT("    cannot play note, instrument {:02x} + note {} = sample {:02x} not loaded\n",
                *_prev_instr, note.value, patch->sample_idx
            );
            return false;
        }

        auto const& sample_maybe = document.samples[patch->sample_idx];
        // If a sample has been loaded to the driver, it must be valid in the document.
        // However there are probably state propagation bugs, so don't crash on release builds.
        assert(sample_maybe);
        if (!sample_maybe) {
            DEBUG_PRINT(
                "    cannot play note, instrument {:02x} + note {} = sample {:02x} loaded but missing from document\n",
                *_prev_instr, note.value, patch->sample_idx
            );
            return false;
        }

        // Write sample index.
        voice_reg8(SPC_DSP::v_srcn, patch->sample_idx);

        // Write ADSR.
        auto adsr = patch->adsr.to_hex();
        voice_reg8(SPC_DSP::v_adsr0, adsr[0]);
        voice_reg8(SPC_DSP::v_adsr1, adsr[1]);

        // Write pitch.
        auto pitch = calc_tuning(chip_driver._freq_table, sample_maybe->tuning, note);
        voice_reg16(SPC_DSP::v_pitchl, pitch);

        DEBUG_PRINT(
            "    instrument {:02x} + note {} = sample {:02x}, adsr {:02x} {:02x}, pitch {:02x} {:02x}\n",
            *_prev_instr,
            note.value,
            _channel_id,
            patch->sample_idx,
            adsr[0],
            adsr[1],
            (uint8_t) pitch,
            (uint8_t) (pitch >> 8));

        return true;
    };

    auto note_cut = [&]() {
        flags.koff |= channel_flag;
        _note_playing = false;
    };

    for (doc::RowEvent const& ev : events) {
        if (ev.instr) {
            DEBUG_PRINT(
                "channel {} instrument change to {:02x}\n", _channel_id, *ev.instr
            );
            _prev_instr = *ev.instr;

            // TODO maybe disable mid-note instrument changes,
            // due to undesirable complexity when writing a hardware driver.
            // Maybe add an explicit "legato" effect or instrument ID.
            if (_note_playing && !ev.note) {
                if (!try_play_note(_prev_note)) {
                    note_cut();
                }
            }
        }
        if (ev.note) {
            doc::Note note = *ev.note;

            if (note.is_valid_note()) {
                DEBUG_PRINT("channel {}, playing note {}\n", _channel_id, note.value);
                _prev_note = note;

                if (try_play_note(_prev_note)) {
                    flags.kon |= channel_flag;
                    _note_playing = true;
                    // TODO save current note's base pitch register, for vibrato and pitch bends
                } else {
                    note_cut();
                }
            } else if (note.is_release()) {
                DEBUG_PRINT("channel {}, note release\n", _channel_id);
                // TODO each instrument should hold a GAIN envelope used for release.
                // TODO upon note release, should _note_playing = false immediately
                // (can't change instruments during release envelopes) or never
                // (subsequent instrument changes without notes waste CPU time)?
                note_cut();
            } else if (note.is_cut()) {
                DEBUG_PRINT("channel {}, note cut\n", _channel_id);
                note_cut();
            }
        }
        if (ev.volume) {
            #ifdef DRIVER_DEBUG
            fmt::print(stderr, "channel {}, volume {}\n", _channel_id, *ev.volume);
            #endif

            // TODO stereo
            voice_reg8(SPC_DSP::v_voll, *ev.volume);
            voice_reg8(SPC_DSP::v_volr, *ev.volume);
        }
        // TODO ev.effects
    }
}

void Spc700Driver::driver_tick(
    doc::Document const& document,
    EnumMap<ChannelID, EventsRef> const& channel_events,
    RegisterWriteQueue &/*mut*/ regs)
{
    Spc700ChipFlags flags{};

    // Clear key-off flags before pushing new events.
    // (koff doesn't automatically clear, only kon does).
    regs.write(SPC_DSP::r_koff, 0x00);

    for (size_t i = 0; i < enum_count<ChannelID>; i++) {
        auto & driver = _channels[i];
        auto & events = channel_events[i];
        driver.tick(document, *this, events, regs, flags);
    }

    if (flags.koff != 0) {
        regs.write(SPC_DSP::r_koff, flags.koff);
    }

    // idk, make sure we write instruments and pitches before writing key-ons?
    // worst-case, if we don't, one wrong sample, a bit of a pop.
    // regs.add_time(CLOCKS_PER_TWO_SAMPLES);

    if (flags.kon != 0) {
        regs.write(SPC_DSP::r_kon, flags.kon);
    }
}

}
