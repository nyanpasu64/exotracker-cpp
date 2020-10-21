#pragma once

#include "gui/app.h"
#include "gui/main_window.h"

namespace gui {

inline app::GuiApp & get_app() {
    return *(app::GuiApp *) QApplication::instance();
}

inline main_window::MainWindow & win() {
    return main_window::MainWindow::get_instance();
}

}
