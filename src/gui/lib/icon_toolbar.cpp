#include "icon_toolbar.h"

#include <QToolButton>
#include <QIcon>

#include <cassert>
#include <cmath>


void IconToolBar::setup_icon_theme() {
    QIcon::setThemeSearchPaths({"C:/Users/nyanpasu/code/exotracker-icons/out"});
    QIcon::setThemeName("exotracker");
}


constexpr static int icon_sizes[] = {16, 22, 24, 32, 48, 64};


static QSize best_size(QSize orig_size) {
    assert(orig_size.height() == orig_size.width());
    double target_size = orig_size.height() * 2 / 3.0;

    int rounded_size;

    for (int size : icon_sizes) {
        if (size >= target_size) {
            rounded_size = size;
            goto end_for;
        }
    }
    rounded_size = (int) target_size;

    end_for:
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
    action->setIcon(QIcon::fromTheme(icon));

    // Set flat button borders.
    toolbar_widget(this, action)->setAutoRaise(!_button_borders);

    return action;
}
