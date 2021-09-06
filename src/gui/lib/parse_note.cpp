#include "parse_note.h"
#include "doc/events.h"

namespace doc {
    using namespace doc::events;
}

namespace gui::lib::parse_note {

QString stripped(const QString &t, int * pos) {
    QStringView text(t);

    const qsizetype s = text.size();
    text = text.trimmed();
    if (pos)
        (*pos) -= (int) (s - text.size());
    return text.toString();
}

// a, b, c, d, e, f, g
static int const arr[] = {
    9, 11, 0, 2, 4, 5, 7
};

constexpr int MAX_OCTAVE = (doc::CHROMATIC_COUNT - 1) / doc::NOTES_PER_OCTAVE;

ParseIntState parse_note_name(
    NoteNameConfig note_cfg, QString & input, int & pos
) {
    input = stripped(input, &pos);

    if (input.size() == 0) {
        return {QValidator::Intermediate};
    }
    {
        bool ok;
        int note = input.toInt(&ok);
        if (!ok) goto not_int;

        if ((size_t) (note) >= doc::CHROMATIC_COUNT) {
            return {QValidator::Invalid};
        } else {
            return {QValidator::Acceptable, note};
        }
    }
    not_int:

    int idx = 0;
    if (idx >= input.size()) return {QValidator::Intermediate};

    QChar diatonic = input[idx++].toLower();
    bool note_valid = 'a' <= diatonic && diatonic <= 'g';
    if (!note_valid) {
        return {QValidator::Invalid};
    }
    int chromatic = arr[diatonic.unicode() - 'a'];

    if (idx >= input.size()) return {QValidator::Intermediate};
    QChar accidental = input[idx];
    if (accidental == '#') {
        chromatic++;
        idx++;
    } else if (accidental == 'b') {
        chromatic--;
        idx++;
    }

    if (idx >= input.size()) return {QValidator::Intermediate};
    if (input[idx] == '-' && idx + 1 == input.size()) {
        return {QValidator::Intermediate};
    }
    {
        auto octave_str = QStringView(input).mid(idx);
        bool ok;
        int octave = octave_str.toInt(&ok);
        if (!ok) {
            return {QValidator::Invalid};
        }

        octave -= note_cfg.gui_bottom_octave;
        if (!(0 <= octave && octave <= MAX_OCTAVE)) {
            return {QValidator::Invalid};
        }

        int note = doc::NOTES_PER_OCTAVE * octave + chromatic;
        if ((size_t) (note) >= doc::CHROMATIC_COUNT) {
            return {QValidator::Invalid};
        } else {
            return {QValidator::Acceptable, note};
        }
    }
}

}
