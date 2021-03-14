#ifdef _WIN32

namespace gui::app {
    class GuiApp;
}

#define Q_DECLARE_PRIVATE_ORIG(Class) \
    inline Class##Private* d_func() \
    { Q_CAST_IGNORE_ALIGN(return reinterpret_cast<Class##Private *>(qGetPtrHelper(d_ptr));) } \
    inline const Class##Private* d_func() const \
    { Q_CAST_IGNORE_ALIGN(return reinterpret_cast<const Class##Private *>(qGetPtrHelper(d_ptr));) } \
    friend class Class##Private;

#include <qglobal.h>
#undef Q_DECLARE_PRIVATE
#define Q_DECLARE_PRIVATE(Class)  Q_DECLARE_PRIVATE_ORIG(Class) friend class gui::app::GuiApp;

#include <QWindow>

#undef Q_DECLARE_PRIVATE
#define Q_DECLARE_PRIVATE(Class)  Q_DECLARE_PRIVATE_ORIG(Class)

#endif

#include "app.h"

#include <verdigris/wobjectimpl.h>

#ifdef _WIN32
#include <QApplication>
#include <QFont>
#include <QtGui/private/qwindow_p.h>
#include <QtGui/private/qguiapplication_p.h>

static void win32_set_font() {
  // On Windows, Qt 5's default system font (MS Shell Dlg 2) is outdated.
  // Interestingly, the QMessageBox font is correct and comes from lfMessageFont
  // (Segoe UI on English computers).
  // So use it for the entire application.
  // This code will become unnecessary and obsolete once we switch to Qt 6.
  QApplication::setFont(QApplication::font("QMessageBox"));
}

#endif

namespace gui::app {

W_OBJECT_IMPL(GuiApp)

void before_create_hooks() {
}

GuiApp::GuiApp(int &argc, char **argv, int flags)
    : QApplication(argc, argv, flags)
{
#ifdef _WIN32
    win32_set_font();

    auto foo = [] (QScreen * screen) {
        // Whenever the screen changes DPI...
        connect(screen, &QScreen::logicalDotsPerInchChanged, screen, [screen] {
            // Send DPI changed event to all windows in that screen.
            const auto allWindows = QGuiApplication::allWindows();
            for (QWindow *window : allWindows) {
                if (!window->isTopLevel() || window->screen() != screen)
                    continue;
                window->d_func()->emitScreenChangedRecursion(screen);
            }
        },
            // But delay QWindowPrivate::emitScreenChangedRecursion(),
            // because QScreen::logicalDotsPerInchChanged() is emitted too early,
            // before QScreenPrivate::updateGeometriesWithSignals() and
            // QGuiApplicationPrivate::resetCachedDevicePixelRatio() are called.
            Qt::QueuedConnection
        );
    };

    // For every current screen...
    auto screens = QGuiApplication::screens();
    for (auto * screen : qAsConst(screens)) {
        foo(screen);
    }

    // And every future added screen (this will not produce duplicate connections
    // since every QScreen gets added exactly once.)
    connect(this, &QGuiApplication::screenAdded, this, foo);
#endif
}

}
