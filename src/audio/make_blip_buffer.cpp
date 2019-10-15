#include "make_blip_buffer.h"

namespace audio {

Blip_Buffer make_blip_buffer(long smp_per_s, long clk_per_s) {
    Blip_Buffer blip;

    // Set output sample rate and buffer length in milliseconds (1/1000 sec, defaults
    // to 1/4 second), then clear buffer. Returns NULL on success, otherwise if there
    // isn't enough memory, returns error without affecting current buffer setup.

    // 0CC ignores the return code (nonzero char* if error). I guess we'll do that too?
    blip.set_sample_rate(smp_per_s);

    // Set number of source time units per second
    blip.clock_rate(clk_per_s);

    return blip;
}

// end namespace
}
