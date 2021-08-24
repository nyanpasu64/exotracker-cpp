#include "adsr_graph.h"

#include <cstdint>

namespace gui::instrument_dialog::adsr_graph {

using NsampT = uint32_t;

static NsampT const PERIODS [32] =
{
   0x1'00'00, // never fires
          2048, 1536,
    1280, 1024,  768,
     640,  512,  384,
     320,  256,  192,
     160,  128,   96,
      80,   64,   48,
      40,   32,   24,
      20,   16,   12,
      10,    8,    6,
       5,    4,    3,
             2,
             1
};


struct Point {
    /// An absolute timestamp in ???.
    NsampT time;

    /// An envelope amplitude within the range [0..0x7ff].
    uint32_t new_amplitude;
};

/// Simulates the ADSR of a note.
///
/// Returns:
/// - A vector of (timestamp, new amplitude at that time).
///   (The S-DSP envelope is stair-stepped.)
/// - The first element is (0, amplitude)
/// - Only the last element's time is >= max_time.
///   (If the amplitude reaches 0 before max_time,
///   there may be zero elements with time >= max_time.)
static std::vector<Point> adsr(Adsr adsr, NsampT max_time) {
    std::vector<Point> out;
    out.push_back(Point{0, 0});

    NsampT now = 0;
    uint32_t level = 0;
    uint32_t prev_level = 0;

    // Adapted from SPC_DSP.cpp, SPC_DSP::run_envelope().
    enum class EnvMode { Release, Attack, Decay, Decay2 };

    EnvMode env_mode = EnvMode::Attack;

    while (true) {
        if (env_mode == EnvMode::Release) {
            if ( (level -= 0x8) < 0 )
                level = 0;
        } else {
            int rate;
            // This function currently only handles ADSR.
            // Using GAIN for exponential release is planned for another function.
            // Manually switching between GAIN modes at composer-controlled times
            // is not a planned feature,
            // and if implemented, plotting it will require EventQueue.
            if (env_mode == EnvMode::Decay2) {
                level--;
                level -= level >> 8;
                rate = adsr.decay_2;
            } else
            if (env_mode == EnvMode::Decay) {
                level--;
                level -= level >> 8;
                rate = adsr.decay_rate * 2 + 0x10;
            } else
            {
                assert(env_mode == EnvMode::Attack);
                rate = adsr.attack_rate * 2 + 1;
                level += rate < 31 ? 0x20 : 0x400;
            }

            // Sustain level
            if ( (level >> 8) == adsr.sustain_level && env_mode == EnvMode::Decay )
                env_mode = EnvMode::Decay2;

            // unsigned cast because linear decrease going negative also triggers this
            if ( (unsigned) level > 0x7FF )
            {
                level = (level < 0 ? 0 : 0x7FF);
                if ( env_mode == EnvMode::Attack )
                    env_mode = EnvMode::Decay;
            }

            if (rate == 0) {
                now = max_time;
                level = prev_level;
            } else {
                now += PERIODS[rate];
            }
            out.push_back(Point{now, level});

            if (now >= max_time) {
                break;
            }
        }
    }

    return out;
}

} // namespace
