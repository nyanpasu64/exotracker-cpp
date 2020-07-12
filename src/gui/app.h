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
    using QApplication::QApplication;

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
    Options const & options() const {
        return _options;
    }

    void set_options(Options options) {
        _options = options;
    }
};

}
