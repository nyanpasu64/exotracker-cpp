#pragma once

#include "doc/instr.h"
#include "gui/lib/docs_palette.h"

#include <QImage>
#include <QWidget>
#include <QToolButton>

#include <cstdint>

namespace doc {
    using namespace doc::instr;
}

namespace gui::instrument_dialog::adsr_graph {

using doc::Adsr;

namespace colors {
    using gui::lib::docs_palette::Hue;

    constexpr Hue ATTACK = Hue::Red;
    constexpr Hue DECAY = Hue::Green;
    constexpr Hue SUSTAIN = Hue::Blue;
    constexpr Hue DECAY2 = Hue::Purple;
    constexpr Hue RELEASE = Hue::Yellow;
}

class AdsrGraph final : public QWidget {
    /// 1 pixel tall image, mapping x-coordinates to background colors.
    QImage _bg_colors;

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

