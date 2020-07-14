#include "config.h"

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

}
