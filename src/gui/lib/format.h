#pragma once

#include "gui/config.h"
#include "doc/events.h"
#include "doc/accidental_common.h"
#include "util/release_assert.h"

#include <QString>
#include <QStringLiteral>
#include <QKeyEvent>

#include <cstdint>
#include <cstdlib>  // div

namespace gui::lib::format {

namespace detail {
    extern const QString hex_digits[16];

    constexpr static size_t NUM_DIATONIC = 7;
    extern const QString diatonic_names[NUM_DIATONIC];

    using MaybeUnsigned = int8_t;
    constexpr MaybeUnsigned NA = -1;
    extern const MaybeUnsigned semitone_diatonics[12];
}

/// Converts a nybble into a single hex character.
[[nodiscard]] inline QString format_hex_1(size_t num) {
    return detail::hex_digits[num & 0x0F];
}

/// Converts a byte into 2 hex characters.
[[nodiscard]] inline QString format_hex_2(size_t wnum) {
    auto num = (uint8_t) wnum;
    return detail::hex_digits[num >> 4] + detail::hex_digits[num & 0x0F];
}

[[nodiscard]] std::optional<uint8_t> hex_from_key(QKeyEvent const & key);

[[nodiscard]] std::optional<char> alphanum_from_key(QKeyEvent const& key);

using gui::config::NoteNameConfig;
using doc::accidental::AccidentalMode;
using doc::events::Note;
using doc::events::Chromatic;

/// Produces a variable-width string for running text,
/// with format "note, accidental (if present), octave".
/// The result will be used as the lower/upper bounds of a keysplit,
/// so pitches 0 and 127 (min/max) are not displayed as notes for clarity.
[[nodiscard]] QString format_note_keysplit(
    NoteNameConfig cfg, AccidentalMode accidental_mode, Chromatic pitch
);

/// Produces a 3-character string for the pattern editor,
/// with format "note, accidental, octave" (eg. C·4).
/// Natural/missing accidentals are rendered with a spacer.
/// Octave -1 is rendered as '-', and octave 10+ is rendered in hex ('A').
/// This is unintuitive and subject to change.
[[nodiscard]] QString format_pattern_note(
    NoteNameConfig cfg, AccidentalMode accidental_mode, Note note
);

/// Produces a 3-character string for the pattern editor,
/// with format "$XX", showing the note's value in hex.
[[nodiscard]] QString format_pattern_noise(Note note);

}
