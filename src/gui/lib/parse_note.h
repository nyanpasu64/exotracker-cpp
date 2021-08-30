#pragma once

#include "gui/config.h"

#include <QValidator>
#include <QString>

namespace gui::lib::parse_note {

struct ParseIntState {
    QValidator::State state;

    /// Ignored unless state == Acceptable.
    int value = 0;
};

using gui::config::NoteNameConfig;

/// Called instead of QSpinBox::validate()/valueFromText().
/// Assumes no prefix, suffix, or special value text.
ParseIntState parse_note_name(
    NoteNameConfig note_cfg, QString & input, int & pos
);

}
