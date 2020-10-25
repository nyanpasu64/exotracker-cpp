#pragma once

#include <QToolBar>

namespace gui::lib {

class IconToolBar : public QToolBar {
    bool _button_borders;

// impl
public:
//    explicit IconToolBar(QString const& title, bool button_borders, QWidget * parent = nullptr);
    explicit IconToolBar(bool button_borders, QWidget * parent = nullptr);

    QAction * add_icon_action(QString alt, QString icon);
};

}
