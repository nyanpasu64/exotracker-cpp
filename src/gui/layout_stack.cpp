#include "gui/layout_stack.h"


namespace layout_stack {

QLabel * create_label(QString label_text, QWidget * parent, QString name) {
    QLabel * label = create_element<QLabel>(parent, name);
    label->setText(label_text);
    return label;
}

}
