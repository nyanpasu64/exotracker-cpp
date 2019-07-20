#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include <memory>

class MainWindowPrivate;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    static std::unique_ptr<MainWindow> make(QWidget * parent = 0);

    MainWindow(QWidget *parent = 0);
    virtual void _() = 0;
    virtual ~MainWindow();

    friend class MainWindowPrivate;
};



#endif // MAINWINDOW_H
