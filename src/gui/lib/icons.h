#pragma once

#include <QIcon>
#include <QString>

namespace gui::lib::icons {
#ifndef icons_INTERNAL
#define icons_INTERNAL private
#endif

/// All icon sizes except for "scalable".
/// The program should render icons at either these sizes,
/// or use the scalable .svg at a larger size.
constexpr int ICON_SIZES[] = {16, 22, 32};

QIcon get_icon(QString name, QSize out_size2);

}
