#pragma once

#include "gui/main_window.h"

namespace gui {

MainWindow & win() {
    return MainWindow::get_instance();
}

}
