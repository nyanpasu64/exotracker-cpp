#pragma once

#include "gui/main_window.h"

namespace gui {

main_window::MainWindow & win() {
    return main_window::MainWindow::get_instance();
}

}
