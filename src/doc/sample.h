#pragma once

#include "events.h"
#include "util/copy_move.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace doc::sample {

using events::Chromatic;

struct SampleTuning {
    // TODO write a way to compute tuning per-note
    uint32_t sample_rate;
    Chromatic root_key;

    /// During .spc compilation, this should be converted into a format
    /// not requiring exp2().
    int16_t detune_cents = 0;
};

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
};
using MaybeSample = std::optional<Sample>;

constexpr size_t MAX_SAMPLES = 256;
struct Samples {
    std::vector<std::optional<Sample>> v;

// impl
    Samples() {
        v.resize(MAX_SAMPLES);
    }

    DEFAULT_COPY(Samples)
    DEFAULT_MOVE(Samples)

    std::optional<Sample> const & operator[](size_t idx) const {
        return v[idx];
    }
    std::optional<Sample> & operator[](size_t idx) {
        return v[idx];
    }
};

using SampleIndex = uint8_t;

}
