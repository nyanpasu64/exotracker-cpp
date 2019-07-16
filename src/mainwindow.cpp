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
#include <QtWidgets/QLineEdit>
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
        l->setContentsMargins(0, 0, 0, 0);

        {append_container(QGroupBox(parent), QFormLayout(w));
            {add_row(QPushButton(parent), QLineEdit(parent));
                left->setText(tr("Top left"));
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


MainWindow::~MainWindow()
{

}
