#include "note_spinbox.h"
#include "gui/lib/format.h"
#include "gui_common.h"
#include "util/defer.h"

#include <QLineEdit>

#include <utility>

namespace gui::lib::note_spinbox {

using gui::lib::format::format_note_keysplit;

NoteSpinBox::NoteSpinBox(FormatFn format, QWidget * parent)
    : QSpinBox(parent)
    , _format_note_name(std::move(format))
{
    setMaximum(doc::CHROMATIC_COUNT - 1);
}

static QString format_note_name(doc::Chromatic note) {
    // Relying on global state isn't *good*, but I don't care,
    // and this doesn't couple the sound engine to the GUI,
    // only the GUI to the main window.

    auto & note_cfg = get_app().options().note_names;
    auto & doc = win().state().document();

    // I should rethink passing through 0 and 127 as numbers... but I don't care.
    return format_note_keysplit(note_cfg, doc.accidental_mode, note);
};

NoteSpinBox::NoteSpinBox(QWidget * parent)
    : NoteSpinBox(format_note_name, parent)
{}

static const QString LONGEST_STR = QStringLiteral("C#-1");

QString NoteSpinBox::textFromValue(int value) const {
    // It's OK (for now) to return different values during sizeHint(),
    // because Q[Abstract]SpinBox doesn't cache textFromValue()'s return value...
    // yay fragile base classes
    if (_show_longest_str) {
        return LONGEST_STR;
    }

    return _format_note_name((doc::Chromatic) value);
}

using gui::lib::parse_note::parse_note_name;

QValidator::State NoteSpinBox::validate(QString & text, int & pos) const  {
    if (_prev_text == text && !text.isEmpty()) {
        return _prev_state.state;
    }

    _prev_text = text;
    _prev_state = parse_note_name(get_app().options().note_names, text, pos);
    return _prev_state.state;
}

int NoteSpinBox::valueFromText(const QString & text) const {
    if (_prev_text == text && !text.isEmpty()) {
        return _prev_state.value;
    }

    QString copy = text;
    int pos = lineEdit()->cursorPosition();
    _prev_text = copy;
    _prev_state = parse_note_name(get_app().options().note_names, copy, pos);
    return _prev_state.value;
}

QSize NoteSpinBox::sizeHint() const {
    _show_longest_str = true;
    defer { _show_longest_str = false; };
    return QSpinBox::sizeHint();
}

QSize NoteSpinBox::minimumSizeHint() const {
    _show_longest_str = true;
    defer { _show_longest_str = false; };
    return QSpinBox::minimumSizeHint();
}

} // namespace
