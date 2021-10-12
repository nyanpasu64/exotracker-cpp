#include "sample_instrs.h"
#include <random>

namespace doc_util::sample_instrs {

Sample periodic_noise() {
    auto rng = std::minstd_rand();
    // std::uniform_int_distribution<uint8_t> is not allowed by the C++ standard
    // or MSVC's headers.
    auto rand_u8 = std::uniform_int_distribution<uint16_t>(0, 0xff);

    std::vector<uint8_t> brr;

    // 128 samples / 16 samples/block = 8 blocks
    constexpr size_t NBLOCK = 8;
    for (size_t i = 0; i < NBLOCK; i++) {
        bool last = (i + 1 == NBLOCK);
        brr.push_back(brr_header(11, 0, last, last));

        // 8 bytes/block
        for (size_t j = 0; j < 8; j++) {
            brr.push_back((uint8_t) rand_u8(rng));
        }
    }

    return Sample {
        .name = "periodic noise",
        .brr = std::move(brr),
        .loop_byte = 0,
        .tuning = SampleTuning {
            .sample_rate = 440 * 128,
            .root_key = A440_MIDI,
        },
    };
}

Sample long_silence() {
    std::vector<uint8_t> brr;

    constexpr size_t NBLOCK = 100;
    for (size_t i = 0; i < NBLOCK; i++) {
        bool last = (i + 1 == NBLOCK);
        brr.push_back(brr_header(11, 0, last, false));

        // 8 bytes/block
        for (size_t j = 0; j < 8; j++) {
            brr.push_back(0);
        }
    }

    return Sample {
        .name = "silent loooooooooooooooooooooooong",
        .brr = std::move(brr),
        .loop_byte = 0,
        .tuning = SampleTuning {
            .sample_rate = 440 * 16,
            .root_key = A440_MIDI,
        },
    };
}

}
