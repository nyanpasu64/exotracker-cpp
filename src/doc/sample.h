#pragma once

#include "events.h"
#include "util/box_array.h"

#ifdef UNITTEST
#include "util/compare.h"
#endif

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace doc::sample {

using events::Chromatic;
using util::box_array::BoxArray;

inline constexpr uint32_t MIN_SAMPLE_RATE = 0;
inline constexpr uint32_t MAX_SAMPLE_RATE = 999'999;

struct SampleTuning {
    // TODO write a way to compute tuning per-note
    uint32_t sample_rate;
    Chromatic root_key;

    /// During .spc compilation, this should be converted into a format
    /// not requiring exp2().
    int16_t detune_cents = 0;

#ifdef UNITTEST
    DEFAULT_EQUALABLE(SampleTuning)
#endif
};

inline constexpr size_t BRR_BLOCK_SIZE = 9;

// TODO copy whatever amktools uses
struct Sample {
    std::string name;

    /// Length should be a multiple of 9. The last block determines whether it loops.
    std::vector<uint8_t> brr;

    // TODO import process:
    // - source data (wav or brr)
    // - import metadata (resampling, volume, etc.)

    /// Should be a multiple of 9.
    /// Ignored if the sample does not loop (except during sample switching).
    uint16_t loop_byte;

    SampleTuning tuning;

// impl
#ifdef UNITTEST
    DEFAULT_EQUALABLE(Sample)
#endif
};
using MaybeSample = std::optional<Sample>;

/// The number of slots is MAX_SAMPLES. (It is an error to resize v.)
/// SampleIndex < Samples.size() == MAX_SAMPLES
constexpr size_t MAX_SAMPLES = 256;
using Samples = BoxArray<std::optional<Sample>, MAX_SAMPLES>;

using SampleIndex = uint8_t;

}
