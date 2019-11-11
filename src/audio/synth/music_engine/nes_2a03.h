#pragma once

#include "../music_engine_common.h"

namespace audio::synth::music_engine::nes_2a03 {

class NesSquareEngine : public SubMusicEngine {

public:
    // impl SubMusicEngine
    void run(RegisterWriteQueue & register_writes) override;
};

// namespace
}
