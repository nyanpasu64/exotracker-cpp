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
    but before we construct and save any QFonts based off the default font.
    This isn't usually a problem, except that Options is constructed before
    GuiApp() runs, and contains some QFont (which in the future, may be based off
    the default font). So we need to either initialize `QApplication::setFont`
    in a base class or field constructed before GuiApp's fields, or not store options
    in GuiApp's fields, or initialize the options later on.

    Another option is to store Options in some sort of "global context" singleton,
    either initialized by GuiApp() or separately.
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
