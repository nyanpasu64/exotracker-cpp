#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "history.h"
#include "doc.h"

#include <verdigris/wobjectdefs.h>

#include <QMainWindow>
#include <memory>


namespace gui {

/// Everything exposed to other modules goes here. GUI widgets/etc. go in MainWindowPrivate.
class MainWindow : public QMainWindow
{
    W_OBJECT(MainWindow);

private:

public:

    // impl
    static std::unique_ptr<MainWindow> make(
        history::History & history, QWidget * parent = nullptr
    );

    static MainWindow & get_instance();

    MainWindow(QWidget *parent = nullptr);
    virtual void _() = 0;
    virtual ~MainWindow();
};


}

#endif // MAINWINDOW_H
