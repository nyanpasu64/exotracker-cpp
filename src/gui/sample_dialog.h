#pragma once

#include "history.h"
#include "gui/main_window.h"

#include <verdigris/wobjectdefs.h>

#include <QDialog>

namespace gui::sample_dialog {

using history::GetDocument;
using main_window::MainWindow;

class SampleDialog : public QDialog {
    W_OBJECT(SampleDialog)
protected:
    // SampleList()
    using QDialog::QDialog;

public:
    /// Holds a persistent aliased reference to MainWindow.
    static SampleDialog * make(MainWindow * win, QWidget * parent = nullptr);

    virtual void reload_state() = 0;

    virtual void update_selection() = 0;
};

// namespace
}
