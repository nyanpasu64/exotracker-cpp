#pragma once

#include "gui/config.h"
#include "doc/events.h"
#include "doc/accidental_common.h"

#include <QString>
#include <QStringLiteral>
#include <cstdint>
#include <cstdlib>  // div

namespace gui::lib::format {

using doc::accidental::AccidentalMode;

namespace detail {
    extern const QString hex_digits[16];

    constexpr static size_t NUM_DIATONIC = 7;
    extern const QString diatonic_names[NUM_DIATONIC];

    constexpr int8_t NA = -1;
    extern const int8_t semitone_diatonics[12];
}

/// Converts a nybble into a single hex character.
static QString format_hex_1(uint8_t num) {
    return detail::hex_digits[num & 0x0F];
}

/// Converts a byte into 2 hex characters.
static QString format_hex_2(uint8_t num) {
    return detail::hex_digits[num >> 4] + detail::hex_digits[num & 0x0F];
}

constexpr int NOTES_PER_OCTAVE = 12;

static QString midi_to_note_name(
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
        auto & impl, doc::events::ChromaticInt note, QChar accidental
    ) -> QString {
        auto [octave, semitone] = div(note, NOTES_PER_OCTAVE);
        octave += cfg.bottom_octave;

        auto diatonic = detail::semitone_diatonics[semitone];
        if (diatonic >= 0) {
            return detail::diatonic_names[diatonic]
                + accidental
                + QString::number(octave);
        } else if (accidental_mode == AccidentalMode::Sharp) {
            return impl(impl, note - 1, cfg.sharp_char);
        } else {
            return impl(impl, note + 1, cfg.flat_char);
        }
    };

    return impl(impl, note.value, cfg.natural_char);
}

}
