#pragma once

#include "history.h"
#include "gui/main_window.h"

#include <verdigris/wobjectdefs.h>

#include <QWidget>

namespace gui::timeline_editor {

using history::GetDocument;
using main_window::MainWindow;

class TimelineEditor : public QWidget {
    W_OBJECT(TimelineEditor)
protected:
    // TimelineEditor() constructor
    using QWidget::QWidget;

public:
    /// Holds a persistent aliased reference to MainWindow.
    static TimelineEditor * make(MainWindow * win, QWidget * parent = nullptr);

    virtual void set_history(GetDocument get_document) = 0;

    virtual void update_cursor() = 0;

    // TODO In order to use QAbstractItemModel,
    //  we need more fine-grained info on modifications.
};

}
