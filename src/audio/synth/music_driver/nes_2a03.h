#pragma once

#include "../music_driver_common.h"

#include <memory>

namespace audio::synth::music_driver::nes_2a03 {

std::unique_ptr<SubMusicDriver> make_Pulse1();
std::unique_ptr<SubMusicDriver> make_Pulse2();

std::unique_ptr<SubMusicDriver> make_Tri();
std::unique_ptr<SubMusicDriver> make_Noise();
std::unique_ptr<SubMusicDriver> make_Dpcm();

// namespace
}
