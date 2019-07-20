#ifndef LOL_DIALOG_H
#define LOL_DIALOG_H

#include <QDialog>

namespace Ui {
class lol_dialog;
}

class LolDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LolDialog(QWidget *parent = 0);
    ~LolDialog();

private:
    Ui::lol_dialog *ui;
};

#endif // LOL_DIALOG_H
