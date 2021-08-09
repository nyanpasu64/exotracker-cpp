#pragma once

#include "sample.h"
#include "events.h"
#include "util/copy_move.h"

#ifdef UNITTEST
#include "util/compare.h"
#endif

#include <array>
#include <string>
#include <cstdint>
#include <optional>
#include <vector>

/// Instrument format.
namespace doc::instr {

using events::Chromatic;

template<class IntT_>
struct Envelope {
    using IntT = IntT_;

    std::vector<IntT> values;

// impl
    static Envelope new_empty() {
        return Envelope{};
    }

    static Envelope from_values(std::vector<IntT> values) {
        return Envelope{.values=values};
    }
};

using ByteEnvelope = Envelope<int8_t>;
using ShortEnvelope = Envelope<int16_t>;

/// An integer which should only take on values within a specific (closed) range.
/// This is purely for documentation. No compile-time or runtime checking is performed.
template<int begin, int end, typename T = uint8_t>
using RangeInclusive = T;

struct Adsr {
    uint8_t attack;
    uint8_t decay;
    uint8_t sustain;
    uint8_t release;

// impl
    static constexpr uint8_t MAX_ATTACK = 0x0f;
    static constexpr uint8_t MAX_DECAY = 0x07;
    static constexpr uint8_t MAX_SUSTAIN = 0x07;
    static constexpr uint8_t MAX_RELEASE = 0x1f;

    /// Based on https://nyanpasu64.github.io/AddmusicK/readme_files/hex_command_reference.html#ADSRInfo.
    std::array<uint8_t, 2> to_hex() const {
        return {
            (uint8_t) (attack | (decay << 4) | 0x80),
            (uint8_t) (release | (sustain << 5)),
        };
    }

#ifdef UNITTEST
    DEFAULT_EQUALABLE(Adsr)
#endif
};

constexpr Adsr DEFAULT_ADSR = {0x0f, 0x00, 0x05, 0x07};


struct InstrumentPatch {
    /// Do not use this patch for pitches below this value.
    Chromatic min_note = 0;
    /// Do not use this patch for pitches above this value.
    /// (TODO what if this is below min_note?)
    Chromatic max_note_inclusive = events::CHROMATIC_COUNT - 1;

    /// The sample to play. If sample missing, acts as a key-off(???).
    sample::SampleIndex sample_idx = 0;

    /// The hardware envelope to use when playing this sample.
    // TODO add GAIN support (either global GAIN, or upon instrument release?)
    Adsr adsr = DEFAULT_ADSR;

//    ByteEnvelope volume{};
//    ShortEnvelope pitch{};
//    ByteEnvelope arpeggio{};
//    ByteEnvelope wave_index{};

// impl
#ifdef UNITTEST
    DEFAULT_EQUALABLE(InstrumentPatch)
#endif
};

/// The maximum number of keysplits in 1 instrument.
/// idx < keysplit.size() <= MAX_KEYSPLITS.
constexpr size_t MAX_KEYSPLITS = 128;
struct Instrument {
    std::string name;

    /// A collection of different samples and ADSR values, along with associated ranges of keys.
    /// Whenever a note plays, the driver scans the array
    /// and picks the first patch including the note.
    /// If none match, each note acts as a key-off(???).
    /// (Note that this algorithm has edge-cases, and care must be taken
    /// to ensure the tracker and SPC driver match.)
    std::vector<InstrumentPatch> keysplit;

// impl
#ifdef UNITTEST
    DEFAULT_EQUALABLE(Instrument)
#endif
};
using MaybeInstrument = std::optional<Instrument>;

/// The number of slots is MAX_INSTRUMENTS. (It is an error to resize v.)
/// idx < Instruments.v.size() == MAX_INSTRUMENTS.
constexpr size_t MAX_INSTRUMENTS = 256;
struct Instruments {
    std::vector<std::optional<Instrument>> v;

// impl
    Instruments() {
        v.resize(MAX_INSTRUMENTS);
    }

    DEFAULT_COPY(Instruments)
    DEFAULT_MOVE(Instruments)

    std::optional<Instrument> const & operator[](size_t idx) const {
        return v[idx];
    }
    std::optional<Instrument> & operator[](size_t idx) {
        return v[idx];
    }

#ifdef UNITTEST
    DEFAULT_EQUALABLE(Instruments)
#endif
};

}
