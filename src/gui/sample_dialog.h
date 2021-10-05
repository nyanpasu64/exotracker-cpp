#pragma once

#include "gui/main_window.h"
#include "gui/lib/persistent_dialog.h"

#include <verdigris/wobjectdefs.h>

namespace gui::sample_dialog {

using main_window::MainWindow;
using gui::lib::persistent_dialog::PersistentDialog;
using doc::SampleIndex;

class SampleDialog : public PersistentDialog {
    W_OBJECT(SampleDialog)
protected:
    // SampleDialog()
    using PersistentDialog::PersistentDialog;

public:
    /// Holds a persistent aliased reference to MainWindow.
    static SampleDialog * make(
        SampleIndex sample, MainWindow * win, QWidget * parent = nullptr
    );

    virtual void reload_state(std::optional<SampleIndex> sample) = 0;
};

// namespace
}
