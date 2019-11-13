#include "nes_2a03.h"

#include <cstdint>

namespace audio::synth::music_engine::nes_2a03 {

// Pulse 1/2 engine

class Apu1PulseEngine : public SubMusicEngine {
    Range<0, 2, uint32_t> _channel_id;

public:
    Apu1PulseEngine(uint32_t channel_id) : _channel_id(channel_id) {}

    // impl SubMusicEngine
    void run(RegisterWriteQueue & register_writes) override {
        // do nothing
    }
};

std::unique_ptr<SubMusicEngine> make_Pulse1() {
    return std::make_unique<Apu1PulseEngine>(0);
}

std::unique_ptr<SubMusicEngine> make_Pulse2() {
    return std::make_unique<Apu1PulseEngine>(1);
}

// Triangle engine

class Apu2TriEngine : public SubMusicEngine {
public:
    // impl SubMusicEngine
    void run(RegisterWriteQueue & register_writes) override {
        // do nothing
    }
};

std::unique_ptr<SubMusicEngine> make_Tri() {
    return std::make_unique<Apu2TriEngine>();
}

// Noise engine

class Apu2NoiseEngine : public SubMusicEngine {
public:
    // impl SubMusicEngine
    void run(RegisterWriteQueue & register_writes) override {
        // do nothing
    }
};

std::unique_ptr<SubMusicEngine> make_Noise() {
    return std::make_unique<Apu2NoiseEngine>();
}

// DPCM engine

class Apu2DpcmEngine : public SubMusicEngine {
public:
    // impl SubMusicEngine
    void run(RegisterWriteQueue & register_writes) override {
        // do nothing
    }
};

std::unique_ptr<SubMusicEngine> make_Dpcm() {
    return std::make_unique<Apu2DpcmEngine>();
}

}   // namespace
