#pragma once

#include <Blip_Buffer/Blip_Buffer.h>


namespace audio {

Blip_Buffer make_blip_buffer(blip_long smp_per_s, blip_long clk_per_s);

// end namespace
}

// Blip_Synth is in the global namespace, so this will be too.
using MyBlipSynth = Blip_Synth<blip_good_quality>;
