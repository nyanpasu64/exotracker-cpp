#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

/// Instrument format.
namespace doc::instr {


template<class IntT_>
struct Envelope {
    using IntT = IntT_;

    std::vector<IntT> values;

    static Envelope new_empty() {
        return Envelope{};
    }

    static Envelope from_values(std::vector<IntT> values) {
        return Envelope{.values=values};
    }
};

using ByteEnvelope = Envelope<int8_t>;
using ShortEnvelope = Envelope<int16_t>;

struct Instrument {
    // TODO implement envelope reuse.
    ByteEnvelope volume;
    ShortEnvelope pitch;
    ByteEnvelope wave_index;
};

constexpr size_t MAX_INSTRUMENTS = 128;
using Instruments = std::array<std::optional<Instrument>, MAX_INSTRUMENTS>;

}
