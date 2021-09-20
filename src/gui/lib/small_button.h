#pragma once

#include <QToolButton>

namespace gui::lib::small_button {

static QToolButton * small_button(const QString &text, QWidget *parent = nullptr) {
    auto w = new QToolButton(parent);
    w->setText(text);
    return w;
}

}
