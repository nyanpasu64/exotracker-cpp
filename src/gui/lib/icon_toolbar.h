#pragma once

#include <QToolBar>

namespace gui::lib::icon_toolbar {

class IconToolBar : public QToolBar {
// impl
public:
//    explicit IconToolBar(QString const& title, QWidget * parent = nullptr);
    explicit IconToolBar(QWidget * parent = nullptr);

    QAction * add_icon_action(QString alt, QString icon);
};

void enable_button_borders(QToolBar * tb);

}
