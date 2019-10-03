#include "main_window.h"
#include "gui/pattern_editor/pattern_editor_panel.h"
#include "lib/lightweight.h"
#include "util/macros.h"

#include <verdigris/wobjectimpl.h>

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QFontComboBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

namespace gui {

using std::unique_ptr;
using std::make_unique;

using gui::pattern_editor::PatternEditorPanel;

W_OBJECT_IMPL(MainWindow)

// module-private
class MainWindowPrivate : public MainWindow {
public: // module-private
    // widget pointers go here, if needed

    PatternEditorPanel pattern;

    MainWindowPrivate(QWidget * parent) : MainWindow(parent) {
        setupUi();
    }

    void _() override {}

    void setupUi() {
        auto w = this;
        {add_central_widget_no_layout(PatternEditorPanel(parent));
        }
    }
};


// public
std::unique_ptr<MainWindow> MainWindow::make(QWidget * parent) {
    return make_unique<MainWindowPrivate>(parent);
}


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent)
{}


MainWindow::~MainWindow()
{}

// namespace
}
