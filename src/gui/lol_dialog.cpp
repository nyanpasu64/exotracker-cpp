#include "lol_dialog.h"
#include "ui_lol_dialog.h"

LolDialog::LolDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::lol_dialog)
{
    ui->setupUi(this);
}

LolDialog::~LolDialog()
{
    delete ui;
}
