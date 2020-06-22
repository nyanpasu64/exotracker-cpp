#pragma once

#include "gui/main_window.h"

namespace gui {

MainWindow & window() {
    return MainWindow::get_instance();
}

}
