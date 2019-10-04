#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <verdigris/wobjectdefs.h>

#include <QMainWindow>
#include <memory>


namespace gui {

class MainWindowPrivate;

/// Everything exposed to other modules goes here. GUI widgets/etc. go in MainWindowPrivate.
class MainWindow : public QMainWindow
{
    W_OBJECT(MainWindow);

public:

    // impl
    static std::unique_ptr<MainWindow> make(QWidget * parent = nullptr);

    MainWindow(QWidget *parent = nullptr);
    virtual void _() = 0;
    virtual ~MainWindow();

    friend class MainWindowPrivate;
};


}

#endif // MAINWINDOW_H
