#include "main_window.h"
#include "gui/pattern_editor/pattern_editor_panel.h"
#include "lib/lightweight.h"

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

    PatternEditorPanel * pattern_editor_panel;

    MainWindowPrivate(history::History & history, QWidget * parent) :
        MainWindow(parent)
    {
        setupUi();
        pattern_editor_panel->set_history(history);
    }

    void _() override {}

    void setupUi() {
        auto w = this;
        {add_central_widget_no_layout(PatternEditorPanel(parent));
            pattern_editor_panel = w;
        }
    }
};


// public
std::unique_ptr<MainWindow> MainWindow::make(
    history::History & history, QWidget * parent
) {
    return make_unique<MainWindowPrivate>(history, parent);
}


MainWindow::MainWindow(QWidget *parent) :
    // I kinda regret using the same name for namespace "history" and member variable "history".
    // But it's only a problem since C++ lacks pervasive `self`.
    QMainWindow(parent)
{}


MainWindow::~MainWindow()
{}

// namespace
}
