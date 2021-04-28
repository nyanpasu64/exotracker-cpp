#pragma once

/// Notes have pitches. That's about it.

#include "doc_common.h"
#include "util/compare.h"

#include <gsl/span>

#include <array>
#include <cstdint>
#include <optional>

namespace doc::events {

// These inline namespaces aren't strictly required, and could be removed if desired.
// Pick an underscored name so it doesn't clash with names I care about.
inline namespace note_ {
    /// Note pitch, as expressed as a MIDI note number.
    /// Valid values are [0..127 (CHROMATIC_COUNT - 1)].
    using Chromatic = uint8_t;
    constexpr int CHROMATIC_COUNT = 128;
    constexpr int NOTES_PER_OCTAVE = 12;

    // TODO add a "chromatic | microtonal" type or "floating-point pitch" type,
    // distinct from "note or cut".

    using NoteInt = int16_t;

    /// Represents a "note" value on a tracker pattern.
    /// Stores either a note pitch, or a note release/cut, or echo buffer, etc.
    struct Note {
        NoteInt value;

        // Implicit conversion constructor.
        // Primarily here for gui::history::dummy_document().
        constexpr Note(NoteInt value) : value(value) {}

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

using InstrumentIndex = uint8_t;

/// SNES supports negative volumes.
/// But keep it unsigned for type-consistency with other fields, and hex display.
/// This way I can write an "edit" function operating on optional<uint8_t>
/// regardless of the field being changed.
using Volume = uint8_t;

using EffColIndex = uint32_t;

inline namespace effects_ {
    constexpr EffColIndex MAX_EFFECTS_PER_EVENT = 8;
    constexpr char EFFECT_NAME_PLACEHOLDER = '0';

    /// An effect name is two ASCII characters (probably limited to alphanumeric).
    ///
    /// TODO for multi-byte effects, use ".." or nullopt as a name.
    using EffectName = std::array<char, 2>;

    /// An effect value is a byte.
    using EffectValue = uint8_t;

    struct Effect {
        EffectName name;
        EffectValue value;

        Effect()
            : name{EFFECT_NAME_PLACEHOLDER, EFFECT_NAME_PLACEHOLDER}
            , value{0}
        {}

        Effect(EffectName name, EffectValue value) : name(name), value(value) {}

        Effect(char const* name, EffectValue value)
            : name{name[0], name[1]}
            , value{value}
        {}

        DEFAULT_EQUALABLE(Effect)
    };

    using MaybeEffect = std::optional<Effect>;

    using EffectList = std::array<MaybeEffect, MAX_EFFECTS_PER_EVENT>;
}

struct RowEvent {
    std::optional<Note> note;
    std::optional<InstrumentIndex> instr = {};
    std::optional<Volume> volume = {};
    EffectList effects = {};

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
        // TODO instr/volume/effects
        os << "}";
        return os;
    }
}

#endif
