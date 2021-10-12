#pragma once

#include <verdigris/wobjectdefs.h>

#include <QDialog>

namespace gui::lib::persistent_dialog {

/// A dialog type intended for persistent non-modal editor windows.
///
/// Unlike QDialog, it doesn't have a "default button" triggered upon pressing Enter.
///
/// Unlike QWidget, on Win32, it's properly centered above the parent window and
/// (as expected) has no minimize/maximize buttons.
///
/// I couldn't fix some problems shared among both QDialog and QWidget.
/// On KWin, clicking the parent window (which raises all dialogs) reorders the dialogs
/// by first-created on top, rather than preserving the stacking order.
/// And on all OSes, closing a dialog activates the parent window rather than the
/// previously-active dialog.
class PersistentDialog : public QDialog {
    W_OBJECT(PersistentDialog)
public:
    explicit PersistentDialog(QWidget * parent = nullptr);

// impl QWidget
protected:
    void setVisible(bool visible) override;
    void keyPressEvent(QKeyEvent * e) override;
};

} // namespace
