#include "icon_toolbar.h"
#include "icons.h"

#include <QToolButton>
#include <QIcon>

#include <cassert>
#include <cmath>
#include <utility>

namespace gui::lib {

static QSize best_size(QSize orig_size) {
    assert(orig_size.height() == orig_size.width());
    double target_size = orig_size.height() * 2 / 3.0;

    int rounded_size;
    if (target_size <= 16) {
        rounded_size = 16;
    } else if (target_size <= 24) {
        rounded_size = 22;
    } else if (target_size <= 32) {
        rounded_size = 32;
    } else {
        rounded_size = (int) target_size;
    }

    return QSize(rounded_size, rounded_size);
}


IconToolBar::IconToolBar(bool button_borders, QWidget * parent)
    : QToolBar(parent)
    , _button_borders{button_borders}
{
    setIconSize(best_size(iconSize()));
}

static QToolButton * toolbar_widget(QToolBar * tb, QAction * action) {
    auto out = qobject_cast<QToolButton *>(tb->widgetForAction(action));
    assert(out);
    return out;
}

QAction * IconToolBar::add_icon_action(QString alt, QString icon) {
    QAction * action = addAction(alt);
    action->setIcon(icons::get_icon(std::move(icon), iconSize()));

    // Set flat button borders.
    toolbar_widget(this, action)->setAutoRaise(!_button_borders);

    return action;
}

}
