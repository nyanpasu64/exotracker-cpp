#pragma once

#include "doc.h"

#include <cpp11-on-multicore/bitfield.h>

#include <cstdint>

namespace doc_util::sample_instrs {

using namespace doc;

/// Create a BRR header loop byte:
/// - gain should be between 0 (silent) and 12 (loudest, may clip) inclusive.
/// - filter should be between 0 and 3. 0 is direct 4-bit PCM,
///   and 1-3 are various IIR filters/predictors.
/// - end and loop control whether this is the last BRR block, and if so,
///   whether to loop or stop.
inline uint8_t brr_header(uint8_t gain, uint8_t filter, bool end, bool loop) {
    BEGIN_BITFIELD_TYPE(BrrHeader, uint8_t)
        ADD_BITFIELD_MEMBER(gain, 4, 4)
        ADD_BITFIELD_MEMBER(filter, 2, 2)
        ADD_BITFIELD_MEMBER(end, 1, 1)
        ADD_BITFIELD_MEMBER(loop, 0, 1)
    END_BITFIELD_TYPE()

    BrrHeader out = 0;
    out.gain = gain;
    out.filter = filter;
    out.end = end;
    out.loop = loop;
    return out;
}

constexpr Chromatic A440_MIDI = 69;

inline Sample pulse_12_5() {
    return Sample {
        .name = "12.5%",
        .brr = {brr_header(11, 0, true, true),
            0x77, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        },
        .loop_byte = 0,
        .tuning = SampleTuning {
            .sample_rate = 440 * 16,
            .root_key = A440_MIDI,
        },
    };
}

inline Sample pulse_25() {
    return Sample {
        .name = "25%",
        .brr = {brr_header(11, 0, true, true),
            0x66, 0x66, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee,
        },
        .loop_byte = 0,
        .tuning = SampleTuning {
            .sample_rate = 440 * 16,
            .root_key = A440_MIDI,
        },
    };
}

inline Sample pulse_50() {
    return Sample {
        .name = "50%",
        .brr = {brr_header(11, 0, true, true),
            0x44, 0x44, 0x44, 0x44, 0xcc, 0xcc, 0xcc, 0xcc,
        },
        .loop_byte = 0,
        .tuning = SampleTuning {
            .sample_rate = 440 * 16,
            .root_key = A440_MIDI,
        },
    };
}

static Sample pulse_50_quiet() {
    return Sample {
        .name = "50 quiet%",
        .brr = {brr_header(10, 0, true, true),
            0x44, 0x44, 0x44, 0x44, 0xcc, 0xcc, 0xcc, 0xcc,
        },
        .loop_byte = 0,
        .tuning = SampleTuning {
            .sample_rate = 440 * 16,
            .root_key = A440_MIDI,
        },
    };
}

inline Sample triangle() {
    return Sample {
        .name = "triangle",
        .brr = {
            // Has a slight DC offset.
            brr_header(11, 0, false, false), 0x01, 0x23, 0x45, 0x67, 0x76, 0x54, 0x32, 0x10,
            brr_header(11, 0, true, true), 0xfe, 0xdc, 0xba, 0x98, 0x89, 0xab, 0xcd, 0xef,
        },
        .loop_byte = 0,
        .tuning = SampleTuning {
            .sample_rate = 440 * 32,
            .root_key = A440_MIDI,
        },
    };
}

inline Sample saw() {
    return Sample {
        .name = "saw",
        .brr = {brr_header(11, 0, true, true),
            // Has a slight DC offset.
            0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        },
        .loop_byte = 0,
        .tuning = SampleTuning {
            .sample_rate = 440 * 16,
            .root_key = A440_MIDI,
        },
    };
}

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

inline ChipChannelSettings spc_chip_channel_settings() {
    // 8 channels of default settings
    return {
        {{}, {}, {}, {}, {}, {}, {}, {}},
    };
}

}
