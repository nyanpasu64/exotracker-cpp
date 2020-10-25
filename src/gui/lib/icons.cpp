#define icons_INTERNAL public
#include "icons.h"

#include <fmt/core.h>

#include <QFile>

#include <stdexcept>

namespace gui::lib::icons {

#define Q QStringLiteral

QIcon get_icon(QString name, QSize out_size2) {
    assert(out_size2.height() == out_size2.width());
    int out_size = out_size2.height();

    for (int size : ICON_SIZES) {
        if (size < out_size) {
            continue;
        }
        auto path = Q(":/icons/%1-%2.png").arg(name).arg(size);
        if (QFile(path).exists()) {
            return QIcon(path);
        }
    }

    auto path = Q(":/icons/%1-scalable.png").arg(name);
    if (QFile(path).exists()) {
        return QIcon(path);
    }

    return QIcon();
}

}
