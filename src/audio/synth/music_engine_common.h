#pragma once

#include "../synth_common.h"
#include "util/macros.h"

namespace audio {
namespace synth {
namespace music_engine {

struct TimeRef {
    DISABLE_COPY_MOVE(TimeRef)
    synth::ClockT time;
};

class SubMusicEngine {
public:
    virtual ~SubMusicEngine();

    // TODO add parameter `TimeRef (const) & time`
    virtual void run(synth::RegisterWriteQueue & register_writes) = 0;
};

}
}
}
