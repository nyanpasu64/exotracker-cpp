#pragma once

#include "timing_common.h"
#include "cmd_queue.h"

namespace audio::callback {

using cmd_queue::AudioCommand;
using timing::MaybeSequencerTime;

struct CallbackInterface {
    virtual ~CallbackInterface() = default;

    virtual AudioCommand * seen_command() const = 0;
    virtual MaybeSequencerTime play_time() const = 0;
};

}
