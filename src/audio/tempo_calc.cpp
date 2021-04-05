#include "tempo_calc.h"

#include <algorithm>
#include <cmath>
#include <cassert>

namespace audio::tempo_calc {

/// SPC clock runs at 1024-ish kHz, S-SMP timers {0,1} run at 8-ish kHz.
constexpr uint32_t CLOCKS_PER_PHASE = 128;

ClockT calc_clocks_per_timer(uint32_t spc_timer_period) {
    return CLOCKS_PER_PHASE * spc_timer_period;
}

uint8_t calc_sequencer_rate(doc::SequencerOptions const& options) {
    /// Slightly above 8000 Hz.
    /// Assuming a sampling rate of 32040 Hz, this has value 8010 Hz.
    constexpr double TIMER_BASE_FREQ = double(CLOCKS_PER_S_IDEAL) / CLOCKS_PER_PHASE;

    double t = options.target_tempo;
    double d = options.spc_timer_period;
    double p = options.ticks_per_beat;

    // See doc.h, SequencerOptions doc comment, for formula explanation.
    double rate = d * p * 256. / 60. / TIMER_BASE_FREQ * t;

    // If we set a rate of 0, the sequencer will never advance
    // (aside from *possibly* an initial tick when playback begins),
    // but the sound driver will still run as normal.
    // But the rate should never be negative,
    // as that would indicate an invalid document or buggy code.
    assert(rate >= 0);

    // Clamp the sequencer rate. Negative rates should never occur,
    // and rates above 255 can occur due to poorly chosen parameters.
    // Clamping them to 255 will make the song play too slowly,
    // but there's no better alternative.
    rate = std::clamp(rate, 0., 255.);
    return (uint8_t) std::round(rate);
}

}

// TODO write inline unit tests
