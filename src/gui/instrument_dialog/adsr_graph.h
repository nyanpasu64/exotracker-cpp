#pragma once

#include "doc/instr.h"
#include "gui/lib/docs_palette.h"

#include <QImage>
#include <QShortcut>
#include <QToolButton>
#include <QWidget>

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
    int _zoom_level;
    Adsr _adsr;
    QToolButton * _zoom_out;
    QToolButton * _zoom_in;
    QToolButton * _zoom_reset;

    QShortcut * _zoom_out_key;
    QShortcut * _zoom_in_key;
    QShortcut * _zoom_reset_key;

public:
    explicit AdsrGraph(QWidget * parent = nullptr);

    void set_adsr(Adsr adsr);

    void zoom_out();
    void zoom_in();
    void zoom_reset();

private:
    qreal get_px_per_s();

// impl QWidget
public:
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void wheelEvent(QWheelEvent * event) override;

    // paintEvent() is a pure function (except for screen output).
    void paintEvent(QPaintEvent * event) override;
};

} // namespace

