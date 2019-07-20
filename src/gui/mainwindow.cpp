#include "mainwindow.h"
#include "mainwindow_view.h"

#include "util/macros.h"


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    view(new MainWindowView)
{
    view->setupUi(this);
}


MainWindow::~MainWindow()
{}
