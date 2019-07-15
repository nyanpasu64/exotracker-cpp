#include "mainwindow.h"

#include "macros.h"
#include "gui/lightweight.h"

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
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    auto w = this;
    {add_central_widget(QWidget(parent), QVBoxLayout(w));
        {append_container(QGroupBox(parent), QVBoxLayout(w));
            {append_widget(QPushButton(parent));
                w->setText(tr("Hello world!"));
            }
        }
    }
}


MainWindow::~MainWindow()
{

}
