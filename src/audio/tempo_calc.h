#pragma once

#include "doc.h"
#include "audio_common.h"
#include "synth_common.h"

#include <cstdint>

namespace audio::tempo_calc {

using synth::NsampT;

/// Nominal sampling rate, used when computing tuning tables and tempos.
/// The user changing the emulated sampling rate rate (and clock rate)
/// should not affect how the driver computes pitches and timers,
/// since that would introduce a source of behavioral discrepancies.
constexpr NsampT SAMPLES_PER_S_IDEAL = 32040;

/// SPC output runs at 32-ish kHz, clock runs at 1024-ish kHz.
constexpr uint32_t CLOCKS_PER_SAMPLE = 32;
constexpr ClockT CLOCKS_PER_S_IDEAL = CLOCKS_PER_SAMPLE * SAMPLES_PER_S_IDEAL;

ClockT calc_clocks_per_timer(uint32_t spc_timer_period);

uint8_t calc_sequencer_rate(doc::SequencerOptions const& options);

}
