#include "util/math.h"

#include <doctest.h>

using util::math::modulo;
using util::math::floordiv;
using util::math::floordiv2;
using util::math::ceildiv;

TEST_CASE("Test floor division and modulo.") {
    // Honestly I'm not sure what's the desired behavior with negative divisors.
    // But it works I guess.

    // 5 == 3 * 1 + 2
    CHECK(floordiv(+5, +3) == 1);
    CHECK(floordiv2(+5, +3) == 1);
    CHECK(modulo(+5, +3) == 2);

    // -5 == 3 * -2 + 1
    CHECK(floordiv(-5, +3) == -2);
    CHECK(floordiv2(-5, +3) == -2);
    CHECK(modulo(-5, +3) == 1);

    // -5 == -3 * 1 + -2
    CHECK(floordiv(-5, -3) == 1);
    CHECK(floordiv2(-5, -3) == 1);
    CHECK(modulo(-5, -3) == -2);

    // 5 == -3 * -2 + -1
    CHECK(floordiv(+5, -3) == -2);
    CHECK(floordiv2(+5, -3) == -2);
    CHECK(modulo(+5, -3) == -1);
}

TEST_CASE("Test ceil division.") {
    CHECK(ceildiv(+5, +3) == 2);
    CHECK(ceildiv(-5, +3) == -1);
}
