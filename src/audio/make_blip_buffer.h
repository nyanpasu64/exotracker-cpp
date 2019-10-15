#pragma once

#include <Blip_Buffer/Blip_Buffer.h>


namespace audio {

Blip_Buffer make_blip_buffer(long smp_per_s, long clk_per_s);

template<int range>
Blip_Synth<blip_good_quality, range>
make_blip_synth(Blip_Buffer & output_blip) {
    Blip_Synth<blip_good_quality, range> synth;

    // This Blip_Synth will write to the supplied output_blip.
    // Blip_Synth's update() API requires that each Blip_Synth
    // hold a mutable reference to Blip_Buffer.
    // Which is illegal in Rust, and implicit state, possibly magical/confusing, in C++.
    // But I don't want to rewrite Blip_Buffer to avoid this issue.
    synth.output(&output_blip);

    return synth;
}

// end namespace
}
