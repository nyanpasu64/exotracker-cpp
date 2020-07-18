#include "config.h"

#include <qkeycode/values.h>

#include <QApplication>

namespace gui::config {

inline namespace visual {

    PatternAppearance default_appearance() {
        PatternAppearance visual;

        visual.pattern_font = QFont("dejavu sans mono", 9);
        visual.pattern_font.setStyleHint(QFont::TypeWriter);

        return visual;
    }

}

inline namespace keys {
    using qkeycode::KeyCode;

    KeyboardRow get_octave_0() {
        return KeyboardRow {
            KeyCode::US_Z,     // C
            KeyCode::US_S,     // C#
            KeyCode::US_X,     // D
            KeyCode::US_D,     // D#
            KeyCode::US_C,     // E
            KeyCode::US_V,     // F
            KeyCode::US_G,     // F#
            KeyCode::US_B,     // G
            KeyCode::US_H,     // G#
            KeyCode::US_N,     // A
            KeyCode::US_J,     // A#
            KeyCode::US_M,     // B
            KeyCode::COMMA,    // C
            KeyCode::US_L,     // C#
            KeyCode::PERIOD,   // D
            KeyCode::SEMICOLON,// D#
            KeyCode::SLASH,    // E
        };
    }

    KeyboardRow get_octave_1() {
        return KeyboardRow {
            KeyCode::US_Q,  // C
            KeyCode::DIGIT2,  // C#
            KeyCode::US_W,  // D
            KeyCode::DIGIT3,  // D#
            KeyCode::US_E,  // E
            KeyCode::US_R,  // F
            KeyCode::DIGIT5,  // F#
            KeyCode::US_T,  // G
            KeyCode::DIGIT6,  // G#
            KeyCode::US_Y,  // A
            KeyCode::DIGIT7,  // A#
            KeyCode::US_U,  // B
            KeyCode::US_I, // C
            KeyCode::DIGIT9, // C#
            KeyCode::US_O, // D
            KeyCode::DIGIT0, // D#
            KeyCode::US_P, // E
        };
    }
}

}
