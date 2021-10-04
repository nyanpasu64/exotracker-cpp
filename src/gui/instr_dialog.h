#pragma once

#include "gui/main_window.h"
#include "gui/lib/persistent_dialog.h"

namespace gui::instr_dialog {

using main_window::MainWindow;
using gui::lib::persistent_dialog::PersistentDialog;

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
class InstrumentDialog : public PersistentDialog {
protected:
    // InstrumentDialog(), cannot be called except by subclass
    using PersistentDialog::PersistentDialog;

public:
    static InstrumentDialog * make(MainWindow * parent_win);

    /// May close the dialog and delete the object!
    virtual void reload_state(bool instrument_switched) = 0;
};

} // namespace
