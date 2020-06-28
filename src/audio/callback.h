#pragma once

#include "timing_common.h"
#include "audio_cmd.h"

namespace audio::callback {

using audio_cmd::AudioCommand;
using timing::MaybeSequencerTime;

struct CallbackInterface {
    virtual ~CallbackInterface() = default;

    virtual AudioCommand * seen_command() const = 0;
    virtual MaybeSequencerTime play_time() const = 0;
};

}
