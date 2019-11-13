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

/// An integer which should only take on values within a specific range.
/// This is purely for documentation. No compile-time or runtime checking is performed.
template<int begin, int end, typename T>
using Range = T;

class SubMusicEngine {
public:
    virtual ~SubMusicEngine() = default;

    // TODO add parameter `TimeRef (const) & time`
    virtual void run(synth::RegisterWriteQueue & register_writes) = 0;
};

}
}
}
