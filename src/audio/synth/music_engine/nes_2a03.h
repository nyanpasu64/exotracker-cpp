#pragma once

#include "../music_engine_common.h"

#include <memory>

namespace audio::synth::music_engine::nes_2a03 {

std::unique_ptr<SubMusicEngine> make_Pulse1();
std::unique_ptr<SubMusicEngine> make_Pulse2();

std::unique_ptr<SubMusicEngine> make_Tri();
std::unique_ptr<SubMusicEngine> make_Noise();
std::unique_ptr<SubMusicEngine> make_Dpcm();

// namespace
}
