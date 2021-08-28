#pragma once

#include "doc/instr.h"
#include "gui/lib/docs_palette.h"

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

    inline QColor bg_color(Hue hue) {
        using gui::lib::docs_palette::Shade;
        using gui::lib::docs_palette::get_color;

        return get_color(hue, qreal(Shade::White) - 0.15);
    }

    constexpr Hue ATTACK = Hue::Red;
    constexpr Hue DECAY = Hue::Yellow;
    constexpr Hue SUSTAIN = Hue::Green;
    constexpr Hue DECAY2 = Hue::Blue;
    constexpr Hue RELEASE = Hue::Magenta;
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

