#pragma once

#include <verdigris/wobjectdefs.h>

#include <QWidget>

namespace gui::lib::persistent_dialog {

class PersistentDialog : public QWidget {
    W_OBJECT(PersistentDialog)
public:
    explicit PersistentDialog(QWidget * parent = nullptr);

// impl QWidget
protected:
    void keyPressEvent(QKeyEvent * e) override;
};

} // namespace
