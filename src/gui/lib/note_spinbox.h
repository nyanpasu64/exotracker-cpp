#pragma once

#include "doc.h"
#include "gui/lib/parse_note.h"

#include <QSpinBox>

#include <functional>

namespace gui::lib::note_spinbox {

using parse_note::ParseIntState;
using doc::Chromatic;

class NoteSpinBox : public QSpinBox {
    using FormatFn = std::function<QString(Chromatic)>;

    FormatFn _format_note_name;

    mutable bool _show_longest_str = false;
    mutable QString _prev_text;
    mutable ParseIntState _prev_state{};

public:
    explicit NoteSpinBox(
        std::function<QString(Chromatic)> format, QWidget * parent = nullptr
    );

// impl QSpinBox
protected:
    QString textFromValue(int value) const override;
    QValidator::State validate(QString &text, int &pos) const override;
    int valueFromText(const QString &text) const override;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
};

} // namespace
