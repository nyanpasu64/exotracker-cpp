#include "persistent_dialog.h"

#include <verdigris/wobjectimpl.h>

#include <QKeyEvent>
#include <QKeySequence>

namespace gui::lib::persistent_dialog {

W_OBJECT_IMPL(PersistentDialog)

PersistentDialog::PersistentDialog(QWidget * parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::Dialog);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
}

void PersistentDialog::keyPressEvent(QKeyEvent * e) {
    if (e->matches(QKeySequence::Cancel)) {
        close();
    }

}

} // namespace
