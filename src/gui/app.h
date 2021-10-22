#pragma once

// Do *not* include any other widgets in this header and create an include cycle.
// Other widgets include app.h, since they rely on GuiApp for data/signals.
#include "config.h"

#include <verdigris/wobjectdefs.h>

#include <QApplication>

namespace gui::app {

using config::Options;

class GuiApp : public QApplication {
    W_OBJECT(GuiApp)

private:
    /*
    On Windows, QFont defaults to MS Shell Dlg 2, which is Tahoma instead of Segoe UI,
    and also HiDPI-incompatible.

    Running `QApplication::setFont(QApplication::font("QMessageBox"))` fixes this,
    but the code must run after QApplication is constructed (otherwise MS Sans Serif),
    but before we construct and save any QFonts.

    This is a hard problem.
    Set the font using a SetFont empty-base-class constructed before Options.

    Ideally I'd store Options in some sort of "global context" singleton
    not bound through inheritance to QApplication.
    */

    Options _options;

    /*
    Not sure how to expose SavedState.
    It should be saved to disk in a single go,
    but individual fields are changed upon user interaction.
    Maybe I'll store the fields individually in GuiApp,
    and write a GuiApp method to load/save from disk.
    */

    // SavedState _state;

public:
    GuiApp(int &argc, char **argv, int = ApplicationFlags);

    Options const & options() const {
        return _options;
    }

    void set_options(Options options) {
        _options = options;
    }

    QString app_name() const;
};

}
