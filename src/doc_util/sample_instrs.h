#pragma once

#include "doc.h"

#include <cpp11-on-multicore/bitfield.h>

#include <cstdint>

namespace doc_util::sample_instrs {

using namespace doc;

Sample pulse_12_5();
Sample pulse_25();
Sample pulse_50();
Sample pulse_50_quiet();
Sample triangle();
Sample saw();
Sample periodic_noise();
Sample long_silence();

/// Fast attack, no decay, lasts forever.
constexpr Adsr INFINITE = { 0xf, 0x0, 0x7, 0x00 };

/// Looks good on ADSR graphs.
constexpr Adsr DEMO = { 0x4, 0x0, 0x2, 0x0d };

/// Fast attack, no decay, long decay2.
constexpr Adsr MUSIC_BOX = { 0xf, 0x0, 0x7, 0x0d };

inline Instrument music_box(SampleIndex sample_idx) {
    return Instrument {
        .name = "Music Box",
        .keysplit = {InstrumentPatch {
            .sample_idx = sample_idx,
            .adsr = MUSIC_BOX,
        }},
    };
}

}
