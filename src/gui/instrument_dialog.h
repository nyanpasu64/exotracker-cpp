#pragma once

#include "gui/main_window.h"

#include <QDialog>

namespace gui::instrument_dialog {

using main_window::MainWindow;

/// Closing the instrument dialog (eg. the user clicking X,
/// deleting the active instrument, or opening a new document),
/// deletes the InstrumentDialog object.
///
/// Assign the return value to a `QPointer<InstrumentDialog> maybe`
/// so the pointer gets set to null when the object is deleted.
///
/// In my testing, the deletion occurs when the event loop next runs (not immediately),
/// but to be safe, never access an InstrumentDialog
/// after closing it or calling reload_state().
/// Wait until the next callback, and then re-verify the QPointer is non-null.
class InstrumentDialog : public QDialog {

public:
    static InstrumentDialog * make(MainWindow * parent_win);

    // InstrumentDialog(), cannot be called except by subclass
    using QDialog::QDialog;

    /// May close the dialog and delete the object!
    virtual void reload_state() = 0;
};

} // namespace
