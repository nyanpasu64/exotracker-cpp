#pragma once

#include <Blip_Buffer/Blip_Buffer.h>

// Blip_Synth is in the global namespace, so this will be too.

/// I performed testing in j0CC-FamiTracker
/// with high-frequency NES triangle or VRC6 pulse waves.
/// If BLIP_PHASE_BITS = 10 or greater, and we use blip_high_quality,
/// there will be practically no aliasing above -90dB.
using MyBlipSynth = Blip_Synth<blip_high_quality>;
