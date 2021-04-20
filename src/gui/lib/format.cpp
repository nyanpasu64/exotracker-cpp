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

}
