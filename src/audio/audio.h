#pragma once

// audio/*.h depends on this file. To avoid circular include, audio.h should NOT include audio/*.h.
#include "util/macros.h"

#include <portaudiocpp/PortAudioCpp.hxx>

#include <memory>
#include <cstdint>

namespace audio {

namespace pa = portaudio;

using Amplitude = int16_t;

// explicitly declared const and not explicitly declared extern has internal linkage
const auto AmplitudeFmt = portaudio::SampleDataFormat::INT16;

}
