#include "validate.h"
#include "util/release_assert.h"
#include "chip_kinds.h"

#include <algorithm>  // std::min, stable_sort

namespace doc::validate {

// # Numeric clamping error handling

/// Our input validation checks require that all equality/ordering comparisons
/// (== != < <= > >=) involving NaN return false.
static_assert(std::numeric_limits<float>::is_iec559, "IEEE 754 required");
static_assert(std::numeric_limits<double>::is_iec559, "IEEE 754 required");

// don't invert the bool conditions, since it won't catch NaN.
// cannot be used for arrays, due to stringification.
#define CLAMP_WARN(OBJ, FIELD, MIN, MAX, STATE) \
    if (!(MIN <= OBJ.FIELD)) { \
        PUSH_WARNING(STATE, \
            "."#FIELD"={} below minimum value {}, clamping", OBJ.FIELD, MIN \
        ); \
        OBJ.FIELD = MIN; \
    } \
    if (!(OBJ.FIELD <= MAX)) { \
        PUSH_WARNING(STATE, \
            "."#FIELD"={} above maximum value {}, clamping", OBJ.FIELD, MAX \
        ); \
        OBJ.FIELD = MAX; \
    }

#define CLAMP_DEFAULT(OBJ, FIELD, MIN, MAX, DEFAULT, STATE) \
    if (!(MIN <= OBJ.FIELD)) { \
        PUSH_WARNING(STATE, \
            "."#FIELD"={} below minimum value {}, defaulting to {}", OBJ.FIELD, MIN, DEFAULT \
        ); \
        OBJ.FIELD = DEFAULT; \
    } \
    if (!(OBJ.FIELD <= MAX)) { \
        PUSH_WARNING(STATE, \
            "."#FIELD"={} above maximum value {}, defaulting to {}", OBJ.FIELD, MAX, DEFAULT \
        ); \
        OBJ.FIELD = DEFAULT; \
    }

#define CLAMP_UNSIGNED_DEFAULT(OBJ, FIELD, MAX, DEFAULT, STATE) \
    if (!(OBJ.FIELD <= MAX)) { \
        PUSH_WARNING(STATE, \
            "."#FIELD"={} not within [0..{}], defaulting to {}", OBJ.FIELD, MAX, DEFAULT \
        ); \
        OBJ.FIELD = DEFAULT; \
    }

#define VALIDATE_CHROMATIC(OBJ, FIELD, DEFAULT, STATE) \
    CLAMP_UNSIGNED_DEFAULT(OBJ, FIELD, CHROMATIC_COUNT - 1, DEFAULT, STATE)

// code

doc::SequencerOptions validate_sequencer_options(
    doc::SequencerOptions options, ErrorState & state
) {
    CLAMP_WARN(options, target_tempo, MIN_TEMPO, MAX_TEMPO, state);
    CLAMP_WARN(options, note_gap_ticks, 0, 2, state);
    CLAMP_WARN(options, ticks_per_beat, MIN_TICKS_PER_BEAT, MAX_TICKS_PER_BEAT, state);
    CLAMP_WARN(options, beats_per_measure, 1, MAX_BEATS_PER_MEASURE, state);
    CLAMP_WARN(options, spc_timer_period, MIN_TIMER_PERIOD, MAX_TIMER_PERIOD, state);
    return options;
}

size_t truncate_frequency_table(ErrorState & state, size_t gen_size) {
    if (gen_size > CHROMATIC_COUNT) {
        PUSH_WARNING(state,
            " too long, size()={} > {}, ignoring extra entries",
            gen_size, CHROMATIC_COUNT
        );
    }
    return std::min(gen_size, CHROMATIC_COUNT);
}

doc::FrequenciesOwned validate_frequency_table(
    ErrorState & state, doc::FrequenciesRef orig_freq_table, size_t valid_size
) {
    release_assert(valid_size <= CHROMATIC_COUNT);
    if (valid_size < CHROMATIC_COUNT) {
        PUSH_WARNING(state,
            " too short, size()={} < {}, padding with placeholder tuning",
            valid_size, CHROMATIC_COUNT
        );
    }

    auto freq_table = equal_temperament();

    for (size_t i = 0; i < valid_size; i++) {
        auto freq = orig_freq_table[i];
        if (MIN_TUNING_FREQ <= freq && freq <= MAX_TUNING_FREQ) {
            freq_table[i] = freq;
        } else {
            PUSH_WARNING(state,
                "[{}]={} invalid (not within [{}, {}]), replacing with placeholder tuning",
                i, freq, MIN_TUNING_FREQ, MAX_TUNING_FREQ
            );
        }
    }

    return freq_table;
}

doc::SampleTuning validate_tuning(ErrorState & state, doc::SampleTuning tuning) {
    CLAMP_DEFAULT(tuning, sample_rate, MIN_SAMPLE_RATE, MAX_SAMPLE_RATE, 32000, state);

    if (tuning.root_key >= CHROMATIC_COUNT) {
        PUSH_WARNING(state,
            ".root_key={} invalid, replacing with middle C (60)", tuning.root_key
        );
        tuning.root_key = 60;
    }

    return tuning;
}

doc::Sample validate_sample(ErrorState & state, doc::Sample sample) {
    auto brr_size = sample.brr.size();
    // Spc700Driver::reload_samples() asserts that brr.size() < 0x10000.
    if (brr_size >= 0x10000) {
        PUSH_WARNING(state, ".brr.size()={} >= 2^16", brr_size);
    }
    if (brr_size % BRR_BLOCK_SIZE != 0) {
        PUSH_WARNING(state, ".brr.size()={} is not a multiple of 9", brr_size);
    }

    auto loop_byte = sample.loop_byte;
    if (loop_byte % BRR_BLOCK_SIZE != 0) {
        PUSH_WARNING(state, ".loop_byte={} is not a multiple of 9", loop_byte);
    }
    // Spc700Driver::reload_samples() asserts that loop_byte < brr.size().
    if (loop_byte >= brr_size) {
        PUSH_WARNING(state,
            ".loop_byte={} >= brr.size()={}, defaulting to 0", loop_byte, brr_size
        );
        sample.loop_byte = 0;
    }

    return sample;
}

size_t truncate_samples(ErrorState & state, size_t gen_nsamp) {
    if (gen_nsamp > MAX_SAMPLES) {
        PUSH_WARNING(state,
            " too long, size()={} > {}, ignoring extra samples", gen_nsamp, MAX_SAMPLES
        );
    }
    return std::min(gen_nsamp, MAX_SAMPLES);
}

InstrumentPatch validate_patch(ErrorState & state, InstrumentPatch patch) {
    VALIDATE_CHROMATIC(patch, min_note, 0, state);

    // See https://nyanpasu64.github.io/AddmusicK/readme_files/hex_command_reference.html#ADSRInfo.
    // I chose to default to a "generic" ADSR curve.
    // TODO pick default ADSR parameters for new instruments, and use those?
    CLAMP_UNSIGNED_DEFAULT(patch, adsr.attack_rate, Adsr::MAX_ATTACK_RATE, 0x0f, state);
    CLAMP_UNSIGNED_DEFAULT(patch, adsr.decay_rate, Adsr::MAX_DECAY_RATE, 0x00, state);
    CLAMP_UNSIGNED_DEFAULT(patch, adsr.sustain_level, Adsr::MAX_SUSTAIN_LEVEL, 0x05, state);
    CLAMP_UNSIGNED_DEFAULT(patch, adsr.decay_2, Adsr::MAX_DECAY_2, 0x07, state);

    return patch;
}

size_t truncate_keysplits(ErrorState & state, size_t gen_nkeysplit) {
    if (gen_nkeysplit > MAX_KEYSPLITS) {
        PUSH_WARNING(state,
            ".keysplit too long, size()={} > {}, truncating",
            gen_nkeysplit, MAX_KEYSPLITS
        );
    }

    return std::min(gen_nkeysplit, MAX_KEYSPLITS);
}

size_t truncate_instruments(ErrorState & state, size_t gen_ninstr) {
    if (gen_ninstr > MAX_INSTRUMENTS) {
        PUSH_WARNING(state,
            " too long, size()={} > {}, ignoring extra instruments",
            gen_ninstr, MAX_INSTRUMENTS
        );
    }

    return std::min(gen_ninstr, MAX_INSTRUMENTS);
}

using chip_common::MAX_NCHIP;

optional<size_t> validate_nchip(ErrorState & state, size_t gen_nchip) {
    if (gen_nchip == 0) {
        PUSH_ERROR(state, " empty, invalid document");
        return {};
    }
    if (gen_nchip > MAX_NCHIP) {
        PUSH_ERROR(state,
            " too long, size()={} > {}, invalid document", gen_nchip, MAX_NCHIP
        );
        return {};
    }

    return gen_nchip;
}

optional<size_t> validate_nchip_matches(
    ErrorState & state, size_t gen_nchip, size_t nchip
) {
    release_assert(nchip != 0);
    release_assert(nchip <= MAX_NCHIP);

    if (gen_nchip != nchip) {
        PUSH_ERROR(state,
            ".size()={} != chips.size()={}, invalid shape", gen_nchip, nchip
        );
        return {};
    }

    return gen_nchip;
}

ChipMetadatas compute_chip_metadata(gsl::span<const ChipKind> chips) {
    ChipMetadatas chips_metadata;
    chips_metadata.reserve(chips.size());

    for (ChipKind chip_kind : chips) {
        static_assert(std::is_same_v<std::underlying_type_t<ChipKind>, uint32_t>);

        release_assert(chip_kind < ChipKind::COUNT);
        chips_metadata.push_back(ChipMetadata {
            .chip_kind = chip_kind,
            .nchan = chip_common::CHIP_TO_NCHAN[(uint32_t) chip_kind],
        });
    }

    return chips_metadata;
}

optional<size_t> validate_nchan_matches(
    ErrorState & state,
    size_t gen_nchan,
    ChipMetadataRef chips_metadata,
    size_t chip_idx)
{
    auto const& metadata = chips_metadata[chip_idx];

    if (gen_nchan != metadata.nchan) {
        PUSH_ERROR(state,
            "[{0}].size()={1} != chips[{0}]={2}'s channel count ({3})",
            chip_idx, gen_nchan, (size_t) metadata.chip_kind, metadata.nchan
        );
        return {};
    }

    return gen_nchan;
}

static bool is_printable(char c) {
    return 32 <= c && c <= 126;
}

doc::Effect validate_effect(ErrorState & state, doc::Effect effect) {
    if (effect.name[0] == 0 || effect.name[1] == 0) {
        PUSH_WARNING(state,
            ".name contains one null byte, not zero (effect) or two (no effect)"
        );
    }
    if (!is_printable(effect.name[0])) {
        PUSH_WARNING(state, ".name[0]={:#x} is not printable", effect.name[0]);
    }
    if (!is_printable(effect.name[1])) {
        PUSH_WARNING(state, ".name[1]={:#x} is not printable", effect.name[1]);
    }

    return effect;
}

TickT validate_anchor_tick(ErrorState & state, TickT time) {
    if (time > MAX_TICK) {
        PUSH_WARNING(state, ".anchor_tick={} too long, clamping", time);
        return MAX_TICK;
    }
    return time;
}

size_t truncate_effects(ErrorState & state, size_t gen_neffect) {
    if (gen_neffect > MAX_EFFECTS_PER_EVENT) {
        PUSH_WARNING(state,
            ".v.effects too long, size()={} > {}, truncating",
            gen_neffect, MAX_EFFECTS_PER_EVENT
        );
    }
    return std::min(gen_neffect, (size_t) MAX_EFFECTS_PER_EVENT);
}

TimedRowEvent validate_event(
    ErrorState & state, TimedRowEvent timed_ev, TickT pattern_length
) {
    auto anchor_tick = timed_ev.anchor_tick;
    if (anchor_tick >= pattern_length) {
        PUSH_WARNING(state,
            ".anchor_tick={} lies beyond pattern length ({} beats), invalid",
            anchor_tick, pattern_length
        );
    }

    auto & note = timed_ev.v.note;
    if (note) {
        if (!note->is_cut() && !note->is_release() && !note->is_valid_note()) {
            PUSH_WARNING(state,
                ".v.note={} is unrecognized, may not play correctly", note->value
            );
        }
    }

    return timed_ev;
}

size_t truncate_events(ErrorState & state, size_t gen_nevent) {
    if (gen_nevent > MAX_EVENTS_PER_PATTERN) {
        PUSH_WARNING(state,
            ".events too long, size()={} > {}, truncating",
            gen_nevent, MAX_EVENTS_PER_PATTERN
        );
    }
    return std::min(gen_nevent, (size_t) MAX_EVENTS_PER_PATTERN);
}

EventList validate_events(ErrorState & state, EventList events) {
    bool must_sort = false;
    auto const n = events.size();

    // Loop over all i where i and i + 1 are valid indices.
    for (size_t i = 0; i + 1 < n; i++) {
        if (events[i + 1].anchor_tick < events[i].anchor_tick) {
            must_sort = true;
            PUSH_WARNING(state,
                "[{}].anchor_tick={} < [{}].anchor_tick={}, sorting",
                i + 1,
                events[i + 1].anchor_tick,
                i,
                events[i].anchor_tick);
        }
        // Should we warn on simultaneous events?
    }

    if (must_sort) {
        std::stable_sort(
            events.begin(),
            events.end(),
            [](TimedRowEvent const& a, TimedRowEvent const& b) {
                return a.anchor_tick < b.anchor_tick;
            });
    }

    return events;
}

optional<Pattern> validate_pattern(ErrorState & state, Pattern pattern) {
    if (pattern.length_ticks < 0) {
        PUSH_ERROR(state, ".length_ticks={} < 0, invalid", pattern.length_ticks);
        return {};
    }
    if (pattern.length_ticks == 0) {
        PUSH_WARNING(state, ".length_ticks=0, probably not what you wanted", pattern.length_ticks);
    }
    if (pattern.length_ticks > MAX_TICK) {
        PUSH_ERROR(state,
            ".length_ticks={} > {}, too high", pattern.length_ticks, MAX_TICK
        );
        return {};
    }

    return pattern;
}

optional<TrackBlock> validate_track_block(ErrorState & state, TrackBlock block) {
    bool has_fatal = false;

    const TickT begin_time = block.begin_tick;
    if (begin_time < 0) {
        PUSH_ERROR(state,
            " starts before begin of song, begin_time={} < 0", begin_time
        );
        has_fatal = true;
    }

    if (block.loop_count == 0) {
        PUSH_WARNING(state,
            " has zero loop_count={}", block.loop_count
        );
    }
    if (block.loop_count > (uint32_t) MAX_TICK) {
        PUSH_ERROR(state,
            ".loop_count={} > {}, too high", block.loop_count, MAX_TICK
        );
        return {};
    }

    // block.pattern.length_ticks is gracefully validated in validate_pattern(), called
    // before this function.
    release_assert(block.pattern.length_ticks >= 0);
    release_assert(block.pattern.length_ticks <= MAX_TICK);

    const auto length_ticks =
        (int64_t) block.loop_count * (int64_t) block.pattern.length_ticks;
    release_assert(length_ticks >= 0);

    if (length_ticks > (int64_t) MAX_TICK) {
        PUSH_ERROR(state,
            " total length = {} ticks > {}, too long", length_ticks, MAX_TICK
        );
        return {};
    }

    const auto end_time = (int64_t) begin_time + length_ticks;
    if (end_time < 0) {
        PUSH_ERROR(state, " end time = {} ticks < 0, invalid", end_time);
        return {};
    }
    if (end_time > (int64_t) MAX_TICK) {
        PUSH_ERROR(state, " end time = {} ticks > {}, too high", end_time, MAX_TICK);
        return {};
    }

    release_assert((int64_t) begin_time <= end_time);
    // TODO how to handle zero-length blocks?

    if (has_fatal) {
        return {};
    } else {
        return block;
    }
}

ChannelSettings validate_channel_settings(
    ErrorState & state, ChannelSettings settings
) {
    CLAMP_WARN(settings, n_effect_col, 1, MAX_EFFECTS_PER_EVENT, state);
    return settings;
}

size_t truncate_blocks(ErrorState & state, size_t gen_nblock) {
    if (gen_nblock > MAX_BLOCKS_PER_TRACK) {
        PUSH_WARNING(state,
            ".blocks too long, size()={} > {}, truncating",
            gen_nblock, MAX_BLOCKS_PER_TRACK
        );
    }
    return std::min(gen_nblock, (size_t) MAX_BLOCKS_PER_TRACK);
}

uint8_t validate_effect_name_chars(ErrorState & state, uint8_t gen_nchar) {
    if (gen_nchar != 1 && gen_nchar != 2) {
        PUSH_WARNING(state,
            "effect_name_chars={} unrecognized (should be 1 or 2), defaulting to 1",
            gen_nchar
        );
        return 1;
    }
    return gen_nchar;
}

}
