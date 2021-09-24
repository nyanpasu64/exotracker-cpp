#pragma once

#include "sample.h"
#include "events.h"
#include "util/box_array.h"

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
using util::box_array::BoxArray;

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
    uint8_t attack_rate;
    uint8_t decay_rate;
    uint8_t sustain_level;
    uint8_t decay_2;

// impl
    static constexpr uint8_t MAX_ATTACK_RATE = 0x0f;
    static constexpr uint8_t MAX_DECAY_RATE = 0x07;
    static constexpr uint8_t MAX_SUSTAIN_LEVEL = 0x07;
    static constexpr uint8_t MAX_DECAY_2 = 0x1f;

    /// Based on https://nyanpasu64.github.io/AddmusicK/readme_files/hex_command_reference.html#ADSRInfo.
    std::array<uint8_t, 2> to_hex() const {
        return {
            (uint8_t) (attack_rate | (decay_rate << 4) | 0x80),
            (uint8_t) (decay_2 | (sustain_level << 5)),
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

    /// A collection of different samples and ADSR values,
    /// along with associated ranges of keys.
    /// Whenever a note plays, the driver scans the vector forwards,
    /// skipping patches whose min key isn't strictly increasing,
    /// and picks the last patch where InstrumentPatch::min_note <= note.
    /// If none match, each note acts as a key-off(???).
    ///
    /// Skipping out-of-order patches is more complex to implement than
    /// allowing them to overshadow earlier patches,
    /// but the error messages and behavior is more intuitive for users (I hope).
    ///
    /// (Note that this algorithm has edge-cases, and care must be taken
    /// to ensure the tracker and SPC driver match.
    /// .spc export will likely remove all out-of-order keysplits ahead of time,
    /// simplifying the hardware driver.)
    ///
    /// In the future, single-note samples/patches will be introduced.
    /// The driver plays the note as usual if min_note == note.
    /// If min_note < note, the driver instead acts like no patches match.
    std::vector<InstrumentPatch> keysplit;

// impl
#ifdef UNITTEST
    DEFAULT_EQUALABLE(Instrument)
#endif
};
using MaybeInstrument = std::optional<Instrument>;

/// The number of slots is MAX_INSTRUMENTS. (It is an error to resize v.)
/// idx < Instruments.size() == MAX_INSTRUMENTS.
constexpr size_t MAX_INSTRUMENTS = 256;
using Instruments = BoxArray<std::optional<Instrument>, MAX_INSTRUMENTS>;

}
