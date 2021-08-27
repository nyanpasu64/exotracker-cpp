#pragma once

#include "doc/instr.h"

#include <QWidget>
#include <QToolButton>

#include <cstdint>

namespace doc {
    using namespace doc::instr;
}

namespace gui::instrument_dialog::adsr_graph {

using doc::Adsr;

constexpr int HI = 255;
constexpr int MID = HI - 6;
constexpr int LO = MID - 6;

constexpr QColor BG_ATTACK = QColor(HI, MID, MID);
constexpr QColor BG_DECAY = QColor(MID, HI, MID);
constexpr QColor BG_SUSTAIN = QColor(HI, HI, LO);  // only used for line.
constexpr QColor BG_DECAY2 = QColor(LO, MID, HI);
constexpr QColor BG_RELEASE = QColor(MID, LO, HI);

/// If h/s/l >= 0, set it.
inline QColor with_hsv(QColor color, qreal h, qreal s, qreal v) {
    // Original colors.
    qreal h0, s0, v0, a;
    color.getHsvF(&h0, &s0, &v0, &a);

    if (h < 0) h = h0;
    if (s < 0) s = s0;
    if (v < 0) v = v0;

    return QColor::fromHsvF(h, s, v, a);
}

/// If h/s/l >= 0, set it.
inline QColor with_hsl(QColor color, qreal h, qreal s, qreal l) {
    // Original colors.
    qreal h0, s0, l0, a;
    color.getHslF(&h0, &s0, &l0, &a);

    if (h < 0) h = h0;
    if (s < 0) s = s0;
    if (l < 0) l = l0;

    return QColor::fromHslF(h, s, l, a);
}

inline QColor with_hue(QColor color, qreal hue) {
    qreal h_, s, l, a;
    color.getHslF(&h_, &s, &l, &a);
    return QColor::fromHslF(hue, s, l, a);
}

/// Scale a color's saturation in HSL space.
inline QColor relight_resaturate(QColor color, qreal lightness, qreal saturation) {
    qreal h, s, l, a;
    color.getHslF(&h, &s, &l, &a);
    return QColor::fromHslF(h, saturation * s, lightness * l, a);
}

/// Darken a color while preserving its HSL saturation.
/// Properly darkening a pastel color blended with white
/// turns it into a fully saturated color.
inline QColor relight(QColor bg, qreal lightness) {
    qreal h, s, l, a;
    bg.getHslF(&h, &s, &l, &a);
    // in HSL, the saturation of a color including at least one 255 is 1.

    return QColor::fromHslF(h, s, l * lightness, a);
}

class AdsrGraph final : public QWidget {
    /// Pixels drawn, per second of envelope.
    qreal _px_per_s;
    Adsr _adsr;
    QToolButton * _zoom_out;
    QToolButton * _reset_zoom;
    QToolButton * _zoom_in;

public:
    explicit AdsrGraph(QWidget * parent = nullptr);

    void set_adsr(Adsr adsr);

    void zoom_out();
    void zoom_in();
    void reset_zoom();

// impl QWidget
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // paintEvent() is a pure function (except for screen output).
    void paintEvent(QPaintEvent *event) override;
    };

} // namespace

