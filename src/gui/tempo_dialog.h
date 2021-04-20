#pragma once

#include "history.h"
#include "main_window.h"

#include <verdigris/wobjectdefs.h>

#include <QDialog>

namespace gui::tempo_dialog {

using history::GetDocument;
using main_window::MainWindow;

class TempoDialog : public QDialog {
    W_OBJECT(TempoDialog)
protected:
    using QDialog::QDialog;

public:
    static TempoDialog * make(GetDocument get_document, MainWindow * parent);
};

}
