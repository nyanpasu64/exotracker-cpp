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
constexpr int MID = 245;
constexpr int LO = 235;

constexpr QColor BG_ATTACK = QColor(HI, MID, MID);
constexpr QColor BG_DECAY = QColor(MID, HI, MID);
constexpr QColor BG_SUSTAIN = QColor(HI, HI, LO);  // only used for line.
constexpr QColor BG_DECAY2 = QColor(LO, MID, HI);
constexpr QColor BG_RELEASE = QColor(MID, LO, HI);

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

