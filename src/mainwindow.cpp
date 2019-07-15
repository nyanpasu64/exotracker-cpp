#include "mainwindow.h"

#include "macros.h"
#include "gui/layout_stack.h"

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


namespace L = layout_stack;


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    L::LayoutStack s(this);
    { auto w = L::append_widget<QPushButton>(s);
        w->setText(tr("uwu"));
    }
}


MainWindow::~MainWindow()
{

}
