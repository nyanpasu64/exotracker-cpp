#include "sample_instrs.h"
#include "util/release_assert.h"

#include <cstdint>
#include <random>
#include <vector>

namespace doc_util::sample_instrs {

/// Create a BRR header loop byte:
/// - gain should be between 0 (silent) and 12 (loudest, may clip) inclusive.
/// - filter should be between 0 and 3. 0 is direct 4-bit PCM,
///   and 1-3 are various IIR filters/predictors.
/// - end and loop control whether this is the last BRR block, and if so,
///   whether to loop or stop.
static uint8_t brr_header(uint8_t gain, uint8_t filter, bool end, bool loop) {
    BEGIN_BITFIELD_TYPE(BrrHeader, uint8_t)
        ADD_BITFIELD_MEMBER(gain, 4, 4)
        ADD_BITFIELD_MEMBER(filter, 2, 2)
        ADD_BITFIELD_MEMBER(end, 1, 1)
        ADD_BITFIELD_MEMBER(loop, 0, 1)
    END_BITFIELD_TYPE()

    BrrHeader out = 0;
    out.gain = gain;
    out.filter = filter;
    out.end = end;
    out.loop = loop;
    return out;
}

constexpr Chromatic A440_MIDI = 69;

/// Generates a looping sample, including BRR data and tuning.
/// Does not support generating loop points other than 0, multi-sample loops,
/// or unlooped samples, because they would complicate the API and tuning code.
static Sample data_to_looped_sample(
    std::string name, std::vector<uint8_t> data, uint8_t gain
) {
    auto nsamp = data.size() * 2;

    release_assert(data.size() % 8 == 0);
    auto nblocks = data.size() / 8;

    std::vector<uint8_t> brr;
    brr.reserve(nblocks * 9);

    auto data_ptr = data.data();

    // Each block is 9 bytes long, containing 1 header byte and 8 data bytes.
    for (size_t block = 0; block < nblocks; block++) {
        bool end = (block + 1 == nblocks);
        bool loop = end;
        brr.push_back(brr_header(gain, 0, end, loop));

        brr.insert(brr.end(), data_ptr, data_ptr + 8);
        data_ptr += 8;
    }

    return Sample {
        .name = std::move(name),
        .brr = std::move(brr),
        .loop_byte = 0,
        .tuning = SampleTuning {
            .sample_rate = 440 * (uint32_t) nsamp,
            .root_key = A440_MIDI,
        },
    };
}

Sample pulse_12_5() {
    return data_to_looped_sample(
        "12.5%",
        {
            0x77, 0x77, 0x77, 0x77,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff,
        },
        11);
}

Sample pulse_25() {
    return data_to_looped_sample(
        "25%",
        {
            0x66, 0x66, 0x66, 0x66,
            0x66, 0x66, 0x66, 0x66,
            0xee, 0xee, 0xee, 0xee,
            0xee, 0xee, 0xee, 0xee,
            0xee, 0xee, 0xee, 0xee,
            0xee, 0xee, 0xee, 0xee,
            0xee, 0xee, 0xee, 0xee,
            0xee, 0xee, 0xee, 0xee,
        },
        11);
}

Sample pulse_50() {
    return data_to_looped_sample(
        "50%",
        {
            0x44, 0x44, 0x44, 0x44,
            0x44, 0x44, 0x44, 0x44,
            0x44, 0x44, 0x44, 0x44,
            0x44, 0x44, 0x44, 0x44,
            0xcc, 0xcc, 0xcc, 0xcc,
            0xcc, 0xcc, 0xcc, 0xcc,
            0xcc, 0xcc, 0xcc, 0xcc,
            0xcc, 0xcc, 0xcc, 0xcc,
        },
        11);
}

Sample pulse_50_quiet() {
    return data_to_looped_sample(
        "50% quiet",
        {
            0x44, 0x44, 0x44, 0x44,
            0x44, 0x44, 0x44, 0x44,
            0x44, 0x44, 0x44, 0x44,
            0x44, 0x44, 0x44, 0x44,
            0xcc, 0xcc, 0xcc, 0xcc,
            0xcc, 0xcc, 0xcc, 0xcc,
            0xcc, 0xcc, 0xcc, 0xcc,
            0xcc, 0xcc, 0xcc, 0xcc,
        },
        10);
}

Sample triangle() {
    // Has a slight DC offset.
    return data_to_looped_sample(
        "Triangle",
        {
            0x01, 0x23, 0x45, 0x67, 0x76, 0x54, 0x32, 0x10,
            0xfe, 0xdc, 0xba, 0x98, 0x89, 0xab, 0xcd, 0xef,
        },
        11);
}

Sample saw() {
    // Has a slight DC offset.
    return data_to_looped_sample(
        "Saw",
        {
            0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
            0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
        },
        11);
}

Sample periodic_noise() {
    auto rng = std::minstd_rand();
    // std::uniform_int_distribution<uint8_t> is not allowed by the C++ standard
    // or MSVC's headers.
    auto rand_u8 = std::uniform_int_distribution<uint16_t>(0, 0xff);

    std::vector<uint8_t> data;
    data.reserve(128);
    for (size_t i = 0; i < 128; i++) {
        data.push_back((uint8_t) rand_u8(rng));
    }

    return data_to_looped_sample("Periodic Noise", std::move(data), 11);
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
