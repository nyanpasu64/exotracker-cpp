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

    const int8_t semitone_diatonics[12] = {
        0, NA, 1, NA, 2, 3, NA, 4, NA, 5, NA, 6
    };
}

}
