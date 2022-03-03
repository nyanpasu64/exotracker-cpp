#include "spc700_driver.h"
#include "spc700_synth.h"
#include "chip_instance_common.h"
#include "doc.h"
#include "doc/effect_names.h"
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
    DEBUG_PRINT("initializing channel {}\n", _channel_id);
}

/// Compute the address of per-voice registers, given our current channel number.
static Address calc_voice_reg(size_t channel_id, Address v_reg) {
    auto channel_addr = Address(channel_id << 4);
    release_assert(v_reg <= 0x09);  // the largest SPC_DSP::v_... value.
    return channel_addr + v_reg;
}

struct ChannelVolume {
    uint8_t volume;
    uint8_t velocity;
};

struct StereoVolume {
    // The DSP interprets as two's complement signed.
    // Use unsigned for consistent UB-free behavior.
    uint8_t left;
    uint8_t right;
};

// Utilities

#define REG(x)  ((size_t) (x))

/// Equivalent to SPC700 `mul ya` followed by discarding a and keeping y.
static inline uint8_t mul_hi(uint8_t a, uint8_t b) {
    return (uint8_t) ((REG(a) * REG(b)) >> 8);
}

struct BytePair {
    uint8_t lower;
    uint8_t upper;
};

static inline uint16_t merge(uint8_t lower, uint8_t upper) {
    return (uint16_t) (REG(lower) | REG(upper) << 8);
}

static inline BytePair split(uint16_t x) {
    return BytePair {
        .lower = (uint8_t) x,
        .upper = (uint8_t) (x >> 8),
    };
}

// Volume calculations

// TODO implement switching between SMW pan table (0..20) and custom table ($00..$20).
constexpr size_t PAN_MAX = 0x20;

/// Indexes 0..32 are valid, and 33 (out of bounds) is read by the SPC assembly
/// on full-scale pan. So we need to store 34 pan table items.
static const uint8_t PAN_TABLE[PAN_MAX + 2] = {
      0,   1,   2,   3,   5,   8,  12,  16,
     21,  27,  33,  40,  47,  55,  63,  72,
     81,  89,  96, 102, 107, 111, 114, 117,
    119, 121, 122, 123, 124, 125, 126, 126,
    127, 127,
};

/// Will be changeable in the future.
constexpr uint8_t MASTER_VOLUME = 0xC0;

static StereoVolume calc_volume_reg(
    ChannelVolume volume, PanState pan, SurroundState surround
) {
    // Based on AddMusicKFF L_1013 (https://github.com/KungFuFurby/AddMusicKFF/blob/ce5316f4c99ae5fa37820a2944376cabf8295543/asm/main.asm#L2627).

    // call L_124D
    uint8_t temp_vol = mul_hi(volume.velocity, volume.volume);
    temp_vol = mul_hi(temp_vol, MASTER_VOLUME);
    temp_vol = mul_hi(temp_vol, temp_vol);

    // L_1019:
    // Ignore pan fade for now.
    // TODO who writes to $5C and determines which channels have volumes rewritten?

    // Skip L_102D.

    // L_103B/CalcChanVolume:
    auto calc_lr_volume = [temp_vol](BytePair pan, bool invert) -> uint8_t {
        uint8_t curr = PAN_TABLE[pan.upper];
        uint8_t next = PAN_TABLE[pan.upper + 1];

        auto multiplier = (uint8_t) (curr + mul_hi(next - curr, pan.lower));

        uint8_t out = mul_hi(multiplier, temp_vol);
        // I'm not implementing AMK's volume multiplier which would go here.
        // We've already lost a lot of resolution by this point.

        if (invert) {
            // lol inverting an unsigned quantity... The ASM does ^$FF, +1.
            // We write an unsigned quantity (representing two's complement signed)
            // to the DSP, which interprets it as two's complement signed,
            // so it works out in the end.
            out = -out;
        }

        return out;
    };

    uint16_t pan_u16 = merge(pan.fraction, pan.value);

    static constexpr uint16_t MAX_PAN16 = PAN_MAX * 0x100;
    // TODO warn on invalid pan?
    if (pan_u16 > MAX_PAN16) {
        pan_u16 = MAX_PAN16;
    }

    uint8_t left = calc_lr_volume(split(MAX_PAN16 - pan_u16), surround.left_invert);
    uint8_t right = calc_lr_volume(split(pan_u16), surround.right_invert);
    return StereoVolume {
        .left = left,
        .right = right,
    };
}

void Spc700ChannelDriver::restore_state(
    doc::Document const& document, RegisterWriteQueue & regs
) const {
    DEBUG_PRINT("  restore_state() channel {}\n", _channel_id);

    write_volume(regs);
    // TODO set GAIN (not used yet).
}

void Spc700ChannelDriver::write_volume(RegisterWriteQueue & regs) const {
    DEBUG_PRINT("    volume {}\n", _prev_volume);

    // TODO how do we store current qXY value or actual velocity?
    // TODO how do we switch velocity tables to change the interpretation of qXY?
    auto volume = ChannelVolume {
        .volume = _prev_volume,
        .velocity = 0xB3,
    };

    // TODO access master volume in Spc700Driver const&?
    StereoVolume vol_regs = calc_volume_reg(volume, _prev_pan, _surround);

    // TODO stereo
    regs.write(calc_voice_reg(_channel_id, SPC_DSP::v_voll), vol_regs.left);
    regs.write(calc_voice_reg(_channel_id, SPC_DSP::v_volr), vol_regs.right);
}

constexpr double CENTS_PER_OCTAVE = 1200.;
using chip_instance::SAMPLES_PER_S_IDEAL;

static uint16_t calc_tuning(
    FrequenciesRef freq_table, doc::SampleTuning const& tuning, doc::Chromatic note
) {
    // At pitch 0x1000, samples are played at the SNES's native rate
    // (1 output sample per input sample, around 32000 Hz).
    double tuning_reg_f = double(tuning.sample_rate) / SAMPLES_PER_S_IDEAL * 0x1000;

    // Increase the pitch by tuning.detune_cents.
    tuning_reg_f *= exp2(double(tuning.detune_cents) / CENTS_PER_OCTAVE);

    // Increase the pitch by the note key relative to the sample's root key.
    release_assert(doc::Note(tuning.root_key).is_valid_note());

    // Use the tuning table to detune notes. (This allows for custom tuning schemes,
    // though not supporting microtonal music not mapped to the chromatic scale.)
    tuning_reg_f *=
        freq_table[(size_t) note] / freq_table[(size_t) tuning.root_key];

    // Pitch registers are played back modulo 0x4000.
    // Clamp out-of-range registers instead of letting them wrap around.
    // (This could be reconfigurable?)
    auto out = (uint16_t) std::round(std::clamp(tuning_reg_f, double(0), double(0x3fff)));

    DEBUG_PRINT("    calc_tuning(): note {} -> pitch register {:04x}\n",
        note, out
    );

    return out;
}

static doc::InstrumentPatch const* find_patch(
    gsl::span<doc::InstrumentPatch const> keysplit, doc::Chromatic note
) {
    // NOTE: Keep in sync with spc_export.cpp#instr::InstrumentMap::amk_instrument().
    int curr_min_note = -1;
    doc::InstrumentPatch const* matching = nullptr;

    // Assumption: keysplit[].min_note is strictly increasing.
    // We skip all patches where this is not the case.
    for (doc::InstrumentPatch const& patch : keysplit) {
        if ((int) patch.min_note <= curr_min_note) {
            continue;
        }
        curr_min_note = patch.min_note;

        // Return the last matching patch (stop when the next patch's min_note
        // exceeds the current note).
        if (note < patch.min_note) {
            return matching;
        } else {
            matching = &patch;
        }
    }
    return matching;
}

/// For some registers, we must wait two full samples worth of clocks
/// to make sure that the S-DSP has seen and processed the register write
/// (see the "every_other_sample" variable).
constexpr ClockT CLOCKS_PER_TWO_SAMPLES = 64;
// TODO do any bad consequences happen if we don't wait 2 samples?
// Is it possible for each tick to be shorter than 2 sample on real hardware?
// If we set a high enough timer rate, then we may not wait 2 samples per tick,
// and ChipInstance::run_chip_for() will truncate our register write
// to prevent it from overflowing the tick.

using doc::effect_names::eff_name;

void Spc700ChannelDriver::run_driver(
    doc::Document const& document,
    Spc700Driver const& chip_driver,
    bool tick_tempo,
    EventsRef events,
    RegisterWriteQueue &/*mut*/ regs,
    Spc700ChipFlags & flags)
{
    // If the sequencer was not ticked, we should not be receiving note events.
    // (If I someday add tempo-independent note cuts, they will be emitted
    // from the driver, not the sequencer's EventsRef.)
    if (!tick_tempo) {
        // TODO release_assert?
        assert(events.empty());
    }

    if (tick_tempo) {
        // TODO move note processing into a separate method, and tick volume slides
        // (crescendos, but not staccatos???) only when the sequencer advances.
    }
    // TODO unconditionally tick vibratos (possibly pitch bends, idk).

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
    auto try_play_note = [&](doc::Chromatic note) -> Success {
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

        auto patch = find_patch(document.instruments[*_prev_instr]->keysplit, note);
        if (!patch) {
            DEBUG_PRINT("    cannot play note, instrument {:02x} does not contain note {}\n",
                *_prev_instr, note
            );
            return false;
        }

        // Check to see if the sample has been loaded into ARAM or not
        // (due to missing sample or ARAM being full).
        if (!chip_driver._samples_valid[patch->sample_idx]) {
            DEBUG_PRINT("    cannot play note, instrument {:02x} + note {} = sample {:02x} not loaded\n",
                *_prev_instr, note, patch->sample_idx
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
                *_prev_instr, note, patch->sample_idx
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
            note,
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

    // TODO test AMK driver to see when volumes are reevaluated ($5C)
    bool volumes_changed = false;

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
                _prev_note = (Chromatic) note.value;

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
            DEBUG_PRINT("channel {}, volume {}\n", _channel_id, *ev.volume);
            _prev_volume = *ev.volume;
            volumes_changed = true;
        }
        // TODO ev.effects
        for (doc::MaybeEffect const& effect : ev.effects) {
            if (!effect) continue;

            if (effect->name == eff_name('Y')) {
                _prev_pan = PanState {
                    .value = effect->value,
                    .fraction = 0,
                };
                volumes_changed = true;
            }
        }
    }

    if (volumes_changed) {
        write_volume(regs);
    }
}

Spc700Driver::Spc700Driver(doc::FrequenciesRef frequencies)
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
    , _freq_table(frequencies)
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
    DEBUG_PRINT("Spc700Driver::reset_state()\n");

    // Reset Spc700Driver and all Spc700ChannelDriver, except for the frequency table.
    auto freq_table = std::move(_freq_table);
    *this = Spc700Driver();
    _freq_table = std::move(freq_table);

    // TODO store "initial" state as member state instead, so "reset synth" =
    // "reset state" + "setup synth". Then when samples are reloaded,
    // we can setup synth without resetting state.

    // Reset Spc700Synth, reinitialize _samples_valid and synth's ARAM,
    // and write default driver state to sound chips.
    reload_samples(document, synth, regs);  // writes SAMPLE_DIR to $5D.
}

Spc700Driver::Spc700Driver()
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
{}

void Spc700Driver::restore_state(
    doc::Document const& document, RegisterWriteQueue & regs
) const {
    DEBUG_PRINT("Spc700Driver::restore_state()\n");

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

    // Restore per-channel state.
    for (size_t i = 0; i < enum_count<ChannelID>; i++) {
        _channels[i].restore_state(document, regs);
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
    DEBUG_PRINT("Spc700Driver::reload_samples()\n");

    // When samples are moved around in RAM, playing notes must be stopped.
    // Reset the APU (stops all notes), then rewrite the current volume/etc.
    // (but not notes) to the APU.

    synth.reset();
    restore_state(document, regs);

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

        uint8_t * ram_64k = synth.ram_64k();

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
            ram_64k[sample_entry_addr + 0] = (uint8_t) sample_start_addr;
            ram_64k[sample_entry_addr + 1] = (uint8_t) (sample_start_addr >> 8);
            ram_64k[sample_entry_addr + 2] = (uint8_t) sample_loop_addr;
            ram_64k[sample_entry_addr + 3] = (uint8_t) (sample_loop_addr >> 8);

            // Write the sample data.
            std::copy(
                smp.brr.begin(),
                smp.brr.begin() + (ptrdiff_t) brr_size_clamped,
                &ram_64k[sample_start_addr]);

            sample_start_addr = sample_end_addr;
            _samples_valid[i] = true;
        }
    }

    // Set base address.
    regs.write(SPC_DSP::r_dir, SAMPLE_DIR >> 8);
}

void Spc700Driver::stop_playback(RegisterWriteQueue /*mut*/& regs) {
    regs.write(SPC_DSP::r_koff, 0xff);

    // This delays future register writes
    // caused by Spc700ChannelDriver::tick() on the same tick.
    regs.wait(CLOCKS_PER_TWO_SAMPLES);
}

void Spc700Driver::run_driver(
    doc::Document const& document,
    bool tick_tempo,
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
        driver.run_driver(document, *this, tick_tempo, events, regs, flags);
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

#ifdef UNITTEST
#include <doctest.h>

namespace audio::synth::spc700_driver {

using namespace doc;

TEST_CASE("Test that keysplits are resolved correctly.") {
    std::vector<InstrumentPatch> keysplit = {
        InstrumentPatch { .min_note = 0 },
        InstrumentPatch { .min_note = 60 },
        InstrumentPatch { .min_note = 72 },
    };

    CHECK_EQ(find_patch(keysplit, 0), &keysplit[0]);
    CHECK_EQ(find_patch(keysplit, 59), &keysplit[0]);
    CHECK_EQ(find_patch(keysplit, 60), &keysplit[1]);
    CHECK_EQ(find_patch(keysplit, 71), &keysplit[1]);
    CHECK_EQ(find_patch(keysplit, 72), &keysplit[2]);
    CHECK_EQ(find_patch(keysplit, CHROMATIC_COUNT - 1), &keysplit[2]);
}

TEST_CASE("Test that keysplits with holes are resolved correctly.") {
    std::vector<InstrumentPatch> keysplit = {
        InstrumentPatch { .min_note = 60 },
        InstrumentPatch { .min_note = 72 },
    };

    CHECK_EQ(find_patch(keysplit, 0), nullptr);
    CHECK_EQ(find_patch(keysplit, 59), nullptr);
    CHECK_EQ(find_patch(keysplit, 60), &keysplit[0]);
    CHECK_EQ(find_patch(keysplit, 71), &keysplit[0]);
    CHECK_EQ(find_patch(keysplit, 72), &keysplit[1]);
    CHECK_EQ(find_patch(keysplit, CHROMATIC_COUNT - 1), &keysplit[1]);
}

TEST_CASE("Test that empty keysplits return nullptr.") {
    std::vector<InstrumentPatch> keysplit;

    CHECK_EQ(find_patch(keysplit, 0), nullptr);
    CHECK_EQ(find_patch(keysplit, 60), nullptr);
    CHECK_EQ(find_patch(keysplit, CHROMATIC_COUNT - 1), nullptr);
}

TEST_CASE("Test that keysplits with out-of-order patches prefer earlier patches.") {
    std::vector<InstrumentPatch> keysplit = {
        InstrumentPatch { .min_note = 60 },
        InstrumentPatch { .min_note = 72 },
        InstrumentPatch { .min_note = 48 },
    };

    CHECK_EQ(find_patch(keysplit, 0), nullptr);

    // Is this really the behavior we want?
    CHECK_EQ(find_patch(keysplit, 48), nullptr);
    CHECK_EQ(find_patch(keysplit, 59), nullptr);

    CHECK_EQ(find_patch(keysplit, 60), &keysplit[0]);
    CHECK_EQ(find_patch(keysplit, 71), &keysplit[0]);
    CHECK_EQ(find_patch(keysplit, 72), &keysplit[1]);
    CHECK_EQ(find_patch(keysplit, CHROMATIC_COUNT - 1), &keysplit[1]);
}

}

#endif
