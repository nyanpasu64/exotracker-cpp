#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

#include <gsl/span>

namespace gui::lib::list_warnings {

// TODO support fractional DPI scaling, using dpi.h.
constexpr QSize ICON_SIZE = QSize(16, 16);

/// Does not return a specific size. Instead you must use
/// QAbstractItemView::setIconSize(ICON_SIZE).
QIcon warning_icon();
QColor warning_bg();
QString warning_tooltip(gsl::span<QString const> warnings);

}
