#pragma once

#include "timing_common.h"

namespace audio::callback {

using timing::MaybeSequencerTime;
using timing::SequencerTime;

struct CallbackInterface {
    virtual ~CallbackInterface() = default;

    virtual MaybeSequencerTime play_time() const = 0;
    virtual void stop_playback() = 0;
    virtual void start_playback(SequencerTime time) = 0;
};

}