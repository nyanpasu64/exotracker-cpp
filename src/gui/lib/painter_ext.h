/// See src/DESIGN.md for documentation about the coordinate system.

#pragma once

#include <QPainter>
#include <QPoint>
#include <QRect>

#include <algorithm>  // std::min
#include <cassert>
// #include <cstdlib>  // abs()
#include <cstdint>


namespace gui::lib::painter_ext {

using i32 = int32_t;
using u32 = uint32_t;

/// Unlike QRect, this class treats the corners as lying on gridlines *between* pixels.
/// So if _x2 - _x1 == 16, then width() == 16 as well,
/// and calling QPainter::fillRect() will paint a 16-pixel-wide rectangle on-screen.
class GridRect {
    /// left
    i32 _x1;
    /// top
    i32 _y1;
    /// right
    i32 _x2;
    /// bottom
    i32 _y2;

    // impl
public:
    explicit GridRect() : _x1{0}, _y1{0}, _x2{0}, _y2{0} {}

    explicit GridRect(i32 x, i32 y, u32 dx, u32 dy) :
        _x1{x}, _y1{y}, _x2{x + i32(dx)}, _y2{y + i32(dy)}
    {}

    static GridRect from_corners(i32 x1, i32 y1, i32 x2, i32 y2) {
        GridRect grid_rect;
        grid_rect._x1 = x1;
        grid_rect._y1 = y1;
        grid_rect._x2 = x2;
        grid_rect._y2 = y2;
        return grid_rect;
    }

    explicit GridRect(QPoint a, QPoint b) {
        // debug assert
        assert(a.x() <= b.x());
        assert(a.y() <= b.y());

        _x1 = std::min(a.x(), b.x());
        _x2 = std::max(a.x(), b.x());

        _y1 = std::min(a.y(), b.y());
        _y2 = std::max(a.y(), b.y());
    }

    explicit GridRect(QPoint a, QSize size) :
        GridRect(a, a + QPoint{size.width(), size.height()})
    {}

    #define GETTER(TYPE, METHOD, EXPR)  inline TYPE METHOD() const { return EXPR; }

    #define GETTER_MUT(TYPE, METHOD, EXPR) \
        GETTER(TYPE, METHOD, EXPR)\
        inline TYPE & METHOD() { return EXPR; }

    GETTER_MUT(i32, x, _x1)
    GETTER_MUT(i32, x1, _x1)
    GETTER_MUT(i32, left, _x1)

    GETTER_MUT(i32, x2, _x2)
    GETTER_MUT(i32, right, _x2)

    GETTER_MUT(i32, y, _y1)
    GETTER_MUT(i32, y1, _y1)
    GETTER_MUT(i32, top, _y1)

    GETTER_MUT(i32, y2, _y2)
    GETTER_MUT(i32, bottom, _y2)

    GETTER(QPoint, left_top, (QPoint{_x1, _y1}))
    GETTER(QPoint, left_bottom, (QPoint{_x1, _y2}))
    GETTER(QPoint, right_top, (QPoint{_x2, _y1}))
    GETTER(QPoint, right_bottom, (QPoint{_x2, _y2}))

    GETTER(u32, dx, u32(_x2 - _x1))
    GETTER(u32, width, dx())

    GETTER(u32, dy, u32(_y2 - _y1))
    GETTER(u32, height, dy())

    inline QSize size() const {
        return QSize{int(dx()), int(dy())};
    }

    #define SETTER(TYPE, METHOD, LHS, RHS)  inline void METHOD(TYPE RHS) { LHS = RHS; }

    SETTER(i32, set_left, _x1, x1)
    SETTER(i32, set_right, _x2, x2)
    SETTER(i32, set_top, _y1, y1)
    SETTER(i32, set_bottom, _y2, y2)

    #define MOVER(METHOD, TARGET, OPPOSITE) \
        void METHOD (int TARGET) { \
            _##OPPOSITE = _##OPPOSITE + TARGET - _##TARGET; \
            _##TARGET = _##TARGET + TARGET - _##TARGET; \
        }

    MOVER(move_left, x1, x2)
    MOVER(move_right, x2, x1)
    MOVER(move_top, y1, y2)
    MOVER(move_bottom, y2, y1)

    inline GridRect adjusted(int dx1, int dy1, int dx2, int dy2) const noexcept {
        return GridRect{
            QPoint{_x1 + dx1, _y1 + dy1}, QPoint{_x2 + dx2, _y2 + dy2}
        };
    }

    inline void adjust(int dx1, int dy1, int dx2, int dy2) noexcept {
        _x1 += dx1;
        _y1 += dy1;
        _x2 += dx2;
        _y2 += dy2;
    }

//    GridRect with_horiz(int x1, int x2) const {
//        GridRect out{*this};
//        out._x1 = x1;
//        out._x2 = x2;
//        return out;
//    }

//    GridRect with_vert(int y1, int y2) const {
//        GridRect out{*this};
//        out._y1 = y1;
//        out._y2 = y2;
//        return out;
//    }

    // Converting from QRect

    /*implicit*/ GridRect(QRect rect) :
        GridRect{rect.x(), rect.y(), u32(rect.width()), u32(rect.height())}
    {}

    operator QRect() const {
        return QRect(x(), y(), int(width()), int(height()));
    }
};


#define FILL_RECT painter.fillRect(rect, painter.pen().color())
/// Draws the left border of a GridRect.
static inline void draw_left_border(QPainter & painter, GridRect rect) {
    rect.set_right(rect.left() + painter.pen().width());
    FILL_RECT;
}

/// Draws the right border of a GridRect.
static inline void draw_right_border(QPainter & painter, GridRect rect) {
    rect.set_left(rect.right() - painter.pen().width());
    FILL_RECT;
}

/// Draws the top border of a GridRect.
static inline void draw_top_border(QPainter & painter, GridRect rect) {
    rect.set_bottom(rect.top() + painter.pen().width());
    FILL_RECT;
}

/// Draws the bottom border of a GridRect.
static inline void draw_bottom_border(QPainter & painter, GridRect rect) {
    rect.set_top(rect.bottom() - painter.pen().width());
    FILL_RECT;
}
#undef FILL_RECT

#define DRAW_BORDER_OVERLOAD(METHOD_NAME) \
    static inline void METHOD_NAME(QPainter & painter, QPoint a, QPoint b) { \
        METHOD_NAME(painter, GridRect{a, b}); \
    }
DRAW_BORDER_OVERLOAD(draw_left_border)
DRAW_BORDER_OVERLOAD(draw_right_border)
DRAW_BORDER_OVERLOAD(draw_top_border)
DRAW_BORDER_OVERLOAD(draw_bottom_border)
#undef DRAW_BORDER_OVERLOAD


/// Draw text anchored to a point, with any alignment relative to that point,
/// with no bounding rectangle needed.
class DrawText {
private:
    int _descent;

public:
    /// Does not hold a reference to f.
    DrawText(QFont const & f) {
        // Is it too expensive to call QFontMetrics() once per draw_text() call?
        // Dunno.
        QFontMetrics metrics{f};
        _descent = metrics.descent();
    }

    void draw_text(
        QPainter & painter,
        qreal const x,
        qreal const y,
        Qt::Alignment align,
        QString const & text,
        QRectF * boundingRect = nullptr
    );

    void draw_text(
        QPainter & painter,
        QPointF const point,
        Qt::Alignment align,
        QString const & text,
        QRectF * boundingRect = nullptr
    ) {
       draw_text(painter, point.x(), point.y(), align, text, boundingRect);
    }
};


class PainterScope {
    QPainter & _painter;

public:
    PainterScope(QPainter & painter) : _painter{painter} {
        _painter.save();
    }

    ~PainterScope() {
        _painter.restore();
    }
};

}
