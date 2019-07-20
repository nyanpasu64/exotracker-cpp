#include "mainwindow.h"
#include "lib/lightweight.h"

#include "util/macros.h"


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


using std::unique_ptr;
using std::make_unique;


// module-private
class MainWindowPrivate : public MainWindow {
public: // module-private
    // widget pointers go here, if needed

    MainWindowPrivate(QWidget * parent) : MainWindow(parent) {
        setupUi();
    }

    void _() override {}

    void setupUi() {
        auto w = this;
        {add_central_widget(QWidget(parent), QVBoxLayout(w));
            l->setContentsMargins(0, 0, 0, 0);

            {append_container(QGroupBox(parent), QFormLayout(w));
                {label_row(tr("Top left"), QLineEdit(parent));
                    right->setText(tr("Top right"));
                }
                {add_row(QPushButton(parent), QHBoxLayout());
                    left->setText(tr("Bottom left"));

                    auto * l = right;
                    {append_widget(QPushButton(parent));
                        w->setText(tr("Bottom right"));
                    }
                    {append_widget(QLineEdit(parent));
                        w->setText(tr("Nyanpasu"));
                    }
                }
            }
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
