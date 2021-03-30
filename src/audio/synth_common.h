#pragma once

#include "event_queue.h"

#include <gsl/span>

#include <cstdint>

namespace audio::synth {

/// This type is used widely, so import to audio::synth.
using event_queue::ClockT;

using NsampT = uint32_t;

constexpr uint32_t STEREO_NCHAN = 2;

/// A sample of digital audio, directly from the S-DSP.
using SpcAmplitude = int16_t;

/// Unfiltered non-oversampled digital audio, directly from the S-DSP.
using WriteTo = gsl::span<SpcAmplitude>;
using NsampWritten = NsampT;

}
