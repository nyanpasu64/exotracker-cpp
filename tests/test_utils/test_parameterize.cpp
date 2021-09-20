#include "test_utils/parameterize.h"

#include <fmt/core.h>

PARAMETERIZE(range_0_3, int, x,
    OPTION(x, 0);
    OPTION(x, 1);
    OPTION(x, 2);
)

PARAMETERIZE(range_0_4, int, x,
    OPTION(x, 0);
    OPTION(x, 1);
    OPTION(x, 2);
    OPTION(x, 3);
)

static int how_many_subcases = 0;
static int sum_x = 0;
static int sum_y = 0;

constexpr int nx = 3;
constexpr int ny = 4;

TEST_CASE("Generate the product set of all subcases.") {
    int x;
    int y;

    PICK(range_0_3(x, range_0_4(y)));
    sum_x += x;
    sum_y += y;

    how_many_subcases++;
}

TEST_CASE("Make sure we receive the product set of all subcases.") {
    CHECK(how_many_subcases == nx * ny);
    CHECK(sum_x == (0 + 1 + 2) * ny);
    CHECK(sum_y == nx * (0 + 1 + 2 + 3));
}

TEST_CASE("Reset.") {
    how_many_subcases = 0;
    sum_x = 0;
    sum_y = 0;
}

TEST_CASE("Generate the product set of all subcases... again.") {
    int x;
    int y;

    PICK(range_0_3(x, range_0_4(y)));
    sum_x += x;
    sum_y += y;

    how_many_subcases++;
}

TEST_CASE("Make sure we receive the product set of all subcases... again.") {
    CHECK(how_many_subcases == nx * ny);
    CHECK(sum_x == (0 + 1 + 2) * ny);
    CHECK(sum_y == nx * (0 + 1 + 2 + 3));
}

