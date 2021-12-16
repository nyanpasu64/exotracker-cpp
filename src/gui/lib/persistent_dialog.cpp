#include "persistent_dialog.h"

#include <verdigris/wobjectimpl.h>

#include <QKeyEvent>
#include <QKeySequence>
#include <QPushButton>

namespace gui::lib::persistent_dialog {

W_OBJECT_IMPL(PersistentDialog)

PersistentDialog::PersistentDialog(QWidget * parent)
    : QDialog(parent)
{
    // TODO remove call, and have dialog creator set Qt::WA_DeleteOnClose
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
}

void PersistentDialog::setVisible(bool visible) {
    if (visible) {
        QList<QPushButton *> const list = findChildren<QPushButton*>();
        for (QPushButton * pb : list) {
            pb->setAutoDefault(false);
        }
    }
    QDialog::setVisible(visible);
}

void PersistentDialog::keyPressEvent(QKeyEvent * e) {
    // Calls reject() if Escape is pressed. Ignore other keypresses.
    if (e->matches(QKeySequence::Cancel)) {
        reject();
    } else {
        e->ignore();
    }

    // We don't want Enter presses to trigger the default button (even though there
    // should be no default button because we called setAutoDefault(false)). To be
    // safe, don't call QDialog::keyPressEvent().
}

} // namespace
