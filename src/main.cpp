#include "gui/mainwindow.h"
#include <QApplication>


using std::unique_ptr;


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    unique_ptr<MainWindow> w = MainWindow::make();
    w->show();

    return a.exec();
}
