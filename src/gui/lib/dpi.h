#pragma once

#include <QPaintDevice>

namespace gui::lib::dpi {

inline qreal dpi_fraction(QPaintDevice * pd) {
    return pd->logicalDpiY() / qreal(96.0);
}

/// TODO find a reliable "window DPI changed" signal
inline qreal dpi_scale(QPaintDevice * pd, qreal distance) {
    return dpi_fraction(pd) * distance;
}

}
