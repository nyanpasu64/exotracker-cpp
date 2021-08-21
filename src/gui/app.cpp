#include "app.h"

#include <verdigris/wobjectimpl.h>

namespace gui::app {

W_OBJECT_IMPL(GuiApp)

static void win32_set_font() {
    #if defined(_WIN32) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // On Windows, Qt 5's default system font (MS Shell Dlg 2) is outdated.
    // Interestingly, the QMessageBox font is correct and comes from lfMessageFont
    // (Segoe UI on English computers).
    // So use it for the entire application.
    // This code will become unnecessary and obsolete once we switch to Qt 6.
    QApplication::setFont(QApplication::font("QMessageBox"));
    #endif
}

GuiApp::GuiApp(int &argc, char **argv, int flags)
    : QApplication(argc, argv, flags)
{
    win32_set_font();
}

}
