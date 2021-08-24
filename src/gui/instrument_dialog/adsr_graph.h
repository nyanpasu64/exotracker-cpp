#pragma once

#include "doc/instr.h"

#include <QWidget>

namespace gui::instrument_dialog::adsr_graph {

using doc::instr::Adsr;

class AdsrGraph : public QWidget {
    /*
    Based off:
    - https://github.com/nyanpasu64/AddmusicK/blob/master/docs/readme_files/hex_command_reference.html
    - https://problemkaputt.de/fullsnes.htm#snesapudspadsrgainenvelope
        - Note that Level>=7E0h is inaccurate according to SPC_DSP.cpp, it's >0x7FF.
    - 3rdparty/snes9x-dsp/SPC_DSP.cpp in this repo

    `state_t::counter` decreases modulo 2048 * 5 * 3.
    At a given `period`, an ADSR tick fires whenever the `counter % period == constant`
    (where the constant varies by whether the period is a multiple of 3, 5, or not).
    I suspect the actual S-DSP chip has separate power-of-2-period timers
    ticked at freq, freq/3, and freq/5, and checks `counters[...] % power-of-2 == 0`
    */

    int zoom;

public:
    // AdsrGraph()
    using QWidget::QWidget;

    void set_adsr(Adsr adsr);

    void reset_zoom();

// impl QWidget
    // paintEvent() is a pure function (except for screen output).
    void paintEvent(QPaintEvent *event) override;
};

} // namespace

