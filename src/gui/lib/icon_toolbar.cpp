#include "icon_toolbar.h"
#include "icons.h"

#include <QToolButton>
#include <QIcon>

#include <cassert>
#include <cmath>

namespace gui::lib::icon_toolbar {

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


IconToolBar::IconToolBar(QWidget * parent)
    : QToolBar(parent)
{
    setIconSize(best_size(iconSize()));
}

QAction * IconToolBar::add_icon_action(QString alt, QString icon) {
    QAction * action = addAction(alt);
    action->setIcon(icons::get_icon(icon, iconSize()));

    return action;
}


void enable_button_borders(QToolBar * tb) {
    auto actions = tb->actions();
    for (QAction * action : qAsConst(actions)) {
        // QToolBar::addWidget() creates a QAction wrapping an arbitrary widget.
        // Ignore actions associated with widgets other than tool buttons.
        if (auto * button = qobject_cast<QToolButton *>(tb->widgetForAction(action))) {
            // autoRaise() == true hides the button borders.
            button->setAutoRaise(false);
        }
    }
}

}
