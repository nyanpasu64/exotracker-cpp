#pragma once

#include "history.h"
#include "gui/main_window.h"
#include "gui/lib/persistent_dialog.h"

#include <verdigris/wobjectdefs.h>

#include <QDialog>

namespace gui::sample_dialog {

using history::GetDocument;
using main_window::MainWindow;
using gui::lib::persistent_dialog::PersistentDialog;

class SampleDialog : public PersistentDialog {
    W_OBJECT(SampleDialog)
protected:
    // SampleList()
    using PersistentDialog::PersistentDialog;

public:
    /// Holds a persistent aliased reference to MainWindow.
    static SampleDialog * make(MainWindow * win, QWidget * parent = nullptr);

    virtual void reload_state() = 0;

    virtual void update_selection() = 0;
};

// namespace
}
