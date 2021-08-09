#include "format.h"

namespace gui::lib::format {

namespace detail {
    const QString hex_digits[16] = {
        QStringLiteral("0"),
        QStringLiteral("1"),
        QStringLiteral("2"),
        QStringLiteral("3"),
        QStringLiteral("4"),
        QStringLiteral("5"),
        QStringLiteral("6"),
        QStringLiteral("7"),
        QStringLiteral("8"),
        QStringLiteral("9"),
        QStringLiteral("A"),
        QStringLiteral("B"),
        QStringLiteral("C"),
        QStringLiteral("D"),
        QStringLiteral("E"),
        QStringLiteral("F"),
    };

    const QString diatonic_names[NUM_DIATONIC] = {
        QStringLiteral("C"),
        QStringLiteral("D"),
        QStringLiteral("E"),
        QStringLiteral("F"),
        QStringLiteral("G"),
        QStringLiteral("A"),
        QStringLiteral("B"),
    };

    const MaybeUnsigned semitone_diatonics[12] = {
        0, NA, 1, NA, 2, 3, NA, 4, NA, 5, NA, 6
    };
}

std::optional<uint8_t> hex_from_key(QKeyEvent const & key) {
    // avoid accidental copies of COW strings 🤢
    QString const text = key.text();
    if (text.isEmpty()) {
        return {};
    }

    ushort c = text[0].toUpper().unicode();

    if ('0' <= c && c <= '9') {
        return c - '0';
    }
    if ('A' <= c && c <= 'F') {
        return c - 'A' + 0xA;
    }

    return {};
}

std::optional<char> alphanum_from_key(QKeyEvent const& key) {
    QString const text = key.text();
    if (text.isEmpty()) {
        return {};
    }

    ushort c = text[0].toUpper().unicode();

    if ('0' <= c && c <= '9') {
        return (char) c;
    }
    if ('A' <= c && c <= 'Z') {
        return (char) c;
    }

    return {};
}

using doc::events::NOTES_PER_OCTAVE;

QString format_note_keysplit(
    NoteNameConfig cfg, AccidentalMode accidental_mode, Chromatic pitch
) {
    if (!Note(pitch).is_valid_note()) {
        return QStringLiteral("%1?").arg(pitch);
    }
    if (pitch == 0) {
        return QStringLiteral("0");
    }
    if (pitch == doc::events::CHROMATIC_COUNT - 1) {
        return QStringLiteral("127");
    }

    auto impl = [&cfg, accidental_mode] (
        auto & impl, int pitch, QString accidental
    ) -> QString {
        // Octave is rounded towards 0, and semitone has the same sign as note.
        auto [octave, semitone] = div(pitch, NOTES_PER_OCTAVE);
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
                + QString::number(octave);
        } else if (accidental_mode == AccidentalMode::Sharp) {
            return impl(impl, pitch - 1, cfg.sharp_char);
        } else {
            return impl(impl, pitch + 1, cfg.flat_char);
        }
    };

    return impl(impl, pitch, QString());
}

QString format_pattern_note(
    NoteNameConfig cfg, AccidentalMode accidental_mode, Note note
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

[[nodiscard]] QString format_pattern_noise(Note note) {
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
