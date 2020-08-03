#include "volume_calc_common.h"
#include <cassert>

namespace audio::synth::volume_calc {

// TODO write tests

int volume_mul_4x4_4(int a, int b) {
    assert(unsigned(a) <= 0xf);
    assert(unsigned(b) <= 0xf);

    // Multiply the two volumes.
    int mul = a * b;
    int out = mul / 0xf;

    // Ensure the product of two nonzero values is nonzero.
    if (mul != 0 && out == 0) {
        out = 1;
    }
    return Volume(out);
}

}


#ifdef UNITTEST
#include <doctest.h>

namespace audio::synth::volume_calc {

TEST_CASE("Test volume_mul_4x4_4() is an identity function when passed 0xf") {
    for (int i = 0; i <= 0xf; i++) {
        CAPTURE(i);
        CHECK(volume_mul_4x4_4(i, 0xf) == i);
        CHECK(volume_mul_4x4_4(0xf, i) == i);
    }
}

TEST_CASE("Test volume_mul_4x4_4() returns zero when passed zero") {
    for (int i = 0x0; i <= 0xf; i++) {
        CAPTURE(i);
        CHECK(volume_mul_4x4_4(i, 0) == 0);
        CHECK(volume_mul_4x4_4(0, i) == 0);
    }
}

TEST_CASE("Test volume_mul_4x4_4() returns nonzero when passed nonzero") {
    for (int i = 1; i <= 0xf; i++) {
        CAPTURE(i);
        for (int j = 1; j <= 0xf; j++) {
            CAPTURE(j);
            int product = volume_mul_4x4_4(i, j);
            CHECK(product != 0);
            CHECK(product <= i);
            CHECK(product <= j);
        }
    }
}

}
#endif
