#pragma once

/// Notes have pitches. That's about it.

#include "doc_common.h"
#include "util/compare.h"

#include <gsl/span>

#include <array>
#include <cstdint>  // int16_t
#include <optional>

namespace doc::events {

// These inline namespaces aren't strictly required, and could be removed if desired.
// Pick an underscored name so it doesn't clash with names I care about.
inline namespace note_ {
    using ChromaticInt = int16_t;
    constexpr int CHROMATIC_COUNT = 128;
    constexpr int NOTES_PER_OCTAVE = 12;

    struct Note {
        ChromaticInt value;

        // Implicit conversion constructor.
        // Primarily here for gui::history::dummy_document().
        constexpr Note(ChromaticInt value) : value(value) {}

        // impl
        EQUALABLE_CONSTEXPR(Note, value)

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

/// SNES supports negative volumes.
/// But keep it unsigned for type-consistency with other fields, and hex display.
/// This way I can write an "edit" function operating on optional<uint8_t>
/// regardless of the field being changed.
using Volume = uint8_t;

struct RowEvent {
    std::optional<Note> note;
    std::optional<InstrumentIndex> instr = {};
    std::optional<Volume> volume = {};
    // TODO []effects

    DEFAULT_EQUALABLE(RowEvent)
};

// end namespace
}

#ifdef UNITTEST

#include <ostream>

namespace doc::events {
    inline std::ostream& operator<< (std::ostream& os, RowEvent const & value) {
        os << "RowEvent{";
        if (value.note.has_value()) {
            Note note = *value.note;
            if (note.is_cut()) {
                os << "note cut";
            } else if (note.is_release()) {
                os << "note release";
            } else if (note.is_valid_note()) {
                os << int(note.value);
            } else {
                os << "invalid note " << int(note.value);
            }
        }  else {
            os << "{}";
        }
        os << "}";
        return os;
    }
}

#endif
