#pragma once

#include <QPaintDevice>

inline qreal dpi_fraction(QPaintDevice * pd) {
    return pd->logicalDpiY() / qreal(96.0);
}

inline qreal dpi_scale(QPaintDevice * pd, qreal distance) {
    return dpi_fraction(pd) * distance;
}
