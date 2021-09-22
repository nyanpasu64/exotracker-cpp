#pragma once

#include "history.h"
#include "gui/main_window.h"

#include <QWidget>

namespace gui::sample_list {

using history::GetDocument;
using main_window::MainWindow;

class SampleList : public QWidget {
protected:
    // SampleList()
    using QWidget::QWidget;

public:
    /// Holds a persistent aliased reference to MainWindow.
    static SampleList * make(MainWindow * win, QWidget * parent = nullptr);

    virtual void reload_state() = 0;

    virtual void update_selection() = 0;
};

// namespace
}
