#pragma once

#include "history.h"
#include "gui/main_window.h"

#include <QWidget>

#include <memory>

namespace gui::timeline_editor {

using history::History;
using main_window::MainWindow;

class TimelineEditor : public QWidget {
    // impl
protected:
    // OrderEditor() constructor
    using QWidget::QWidget;

public:
    static TimelineEditor * make(MainWindow * win, QWidget * parent = nullptr);

    virtual void set_history(History const& history) = 0;

    virtual void update_cursor() = 0;

    // TODO In order to use QAbstractItemModel,
    //  we need more fine-grained info on modifications.
};

}
