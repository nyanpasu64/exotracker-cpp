#pragma once

#include <QToolBar>

class IconToolBar : public QToolBar {
    bool _button_borders;

    // impl
public:
    static void setup_icon_theme();

//    explicit IconToolBar(QString const& title, bool button_borders, QWidget * parent = nullptr);
    explicit IconToolBar(bool button_borders, QWidget * parent = nullptr);

    QAction * add_icon_action(QString alt, QString icon);
};
