#pragma once

/// Notes have pitches. That's about it.

#include "util/compare.h"

#include <gsl/span>

#include <array>
#include <cstdint>  // int16_t
#include <optional>

namespace doc::events {

// These inline namespaces aren't strictly required, and could be removed if desired.
inline namespace note {
    using ChromaticInt = int16_t;
    constexpr int CHROMATIC_COUNT = 128;

    struct Note {
        ChromaticInt value;

        // Implicit conversion constructor.
        // Primarily here for gui::history::dummy_document().
        constexpr Note(ChromaticInt value) : value(value) {}

        // impl
        EQUALABLE(Note, (value))

        [[nodiscard]] constexpr bool is_cut() const;
        [[nodiscard]] constexpr bool is_release() const;

        /// Returns true if note.value is an in-bounds array index,
        /// not a cut/release, negative value, or out-of-bounds index.
        [[nodiscard]] constexpr bool is_valid_note() const;
    };

    constexpr Note NOTE_CUT{-1};
    constexpr Note NOTE_RELEASE{-2};

    constexpr bool Note::is_cut() const {
        return *this == NOTE_CUT;
    }

    constexpr bool Note::is_release() const {
        return *this == NOTE_RELEASE;
    }

    constexpr bool Note::is_valid_note() const {
        return 0 <= value && (size_t) (value) < CHROMATIC_COUNT;
    }
}

// Tuning table types
inline namespace tuning {
    using ChromaticInt = ChromaticInt;
    using FreqDouble = double;
    using RegisterInt = int;

    template<typename T>
    using Owned_ = std::array<T, CHROMATIC_COUNT>;

    template<typename T>
    using Ref_ = gsl::span<T const, CHROMATIC_COUNT>;

    using FrequenciesOwned = Owned_<FreqDouble>;
    using FrequenciesRef = Ref_<FreqDouble>;

    using TuningOwned = Owned_<RegisterInt>;
    using TuningRef = Ref_<RegisterInt>;

    FrequenciesOwned equal_temperament(
        ChromaticInt root_chromatic = 69, FreqDouble root_frequency = 440.
    );
}

struct RowEvent {
    std::optional<events::Note> note;
    // TODO volumes and []effects

    EQUALABLE(RowEvent, (note))
};

// end namespace
}
