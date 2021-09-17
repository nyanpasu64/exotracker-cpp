#pragma once

#include "history.h"
#include "gui/main_window.h"

#include <verdigris/wobjectdefs.h>

#include <QWidget>

namespace gui::instrument_list {

using history::GetDocument;
using main_window::MainWindow;

class InstrumentList : public QWidget {
    W_OBJECT(InstrumentList)
protected:
    // InstrumentList() constructor
    using QWidget::QWidget;

public:
    /// Holds a persistent aliased reference to MainWindow.
    static InstrumentList * make(MainWindow * win, QWidget * parent = nullptr);

    virtual void reload_state() = 0;

    virtual void update_selection() = 0;
};

// namespace
}
