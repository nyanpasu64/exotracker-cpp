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

using doc::accidental::AccidentalMode;

namespace detail {
    extern const QString hex_digits[16];

    constexpr static size_t NUM_DIATONIC = 7;
    extern const QString diatonic_names[NUM_DIATONIC];

    using MaybeUnsigned = int8_t;
    constexpr MaybeUnsigned NA = -1;
    extern const MaybeUnsigned semitone_diatonics[12];
}

/// Converts a nybble into a single hex character.
[[nodiscard]] inline QString format_hex_1(uint8_t num) {
    return detail::hex_digits[num & 0x0F];
}

/// Converts a byte into 2 hex characters.
[[nodiscard]] inline QString format_hex_2(uint8_t num) {
    return detail::hex_digits[num >> 4] + detail::hex_digits[num & 0x0F];
}

[[nodiscard]] std::optional<uint8_t> hex_from_key(QKeyEvent const & key);

[[nodiscard]] std::optional<char> alphanum_from_key(QKeyEvent const& key);

using doc::events::NOTES_PER_OCTAVE;

[[nodiscard]] inline QString midi_to_note_name(
    config::NoteNameConfig cfg, AccidentalMode accidental_mode, doc::events::Note note
) {
    if (note.is_cut()) {
        return QStringLiteral("---");
    }
    if (note.is_release()) {
        return QStringLiteral("===");
    }
    if (!note.is_valid_note()) {
        return QStringLiteral("%1?").arg(note.value);
    }

    auto impl = [&cfg, accidental_mode] (
        auto & impl, int note, QChar accidental
    ) -> QString {
        // Octave is rounded towards 0, and semitone has the same sign as note.
        auto [octave, semitone] = div(note, NOTES_PER_OCTAVE);
        // Ensure that octave is correct and semitone is non-negative.
        // (This can only happen if note < 0, which never happens if
        // midi_to_note_name() only receives valid MIDI pitches.
        // So this is just future-proofing.)
        if (semitone < 0) {
            semitone += NOTES_PER_OCTAVE;
            octave -= 1;
        }

        octave += cfg.gui_bottom_octave;

        auto maybe_diatonic = detail::semitone_diatonics[semitone];
        if (maybe_diatonic >= 0) {
            return detail::diatonic_names[maybe_diatonic]
                + accidental
                + (octave < 0 ? QStringLiteral("-") : format_hex_1(octave & 0xf));
        } else if (accidental_mode == AccidentalMode::Sharp) {
            return impl(impl, note - 1, cfg.sharp_char);
        } else {
            return impl(impl, note + 1, cfg.flat_char);
        }
    };

    return impl(impl, note.value, cfg.natural_char);
}

[[nodiscard]] inline QString midi_to_noise_name(doc::events::Note note) {
    if (note.is_cut()) {
        return QStringLiteral("---");
    }
    if (note.is_release()) {
        return QStringLiteral("===");
    }
    if (!note.is_valid_note()) {
        return QStringLiteral("%1?").arg(note.value);
    }

    return QStringLiteral("$%1").arg(format_hex_2((uint8_t) note.value));
}

}
