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
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::Dialog);
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
    if (e->matches(QKeySequence::Cancel)) {
        close();
    }

}

} // namespace
