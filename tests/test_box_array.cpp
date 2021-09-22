#include "util/box_array.h"
#include <doctest.h>

#include <utility>

using util::box_array::BoxArray;

TEST_CASE("Ensure BoxArray is default-initialized") {
    BoxArray<int, 1024> arr;
    CHECK_EQ(arr[0], 0);
    CHECK_EQ(arr[1023], 0);
}

TEST_CASE("Ensure BoxArray can be constructed from a fixed-size span") {
    BoxArray<int, 1024> arr;
    arr[0] = 42;
    arr[1023] = 84;

    auto arr2 = BoxArray(arr.span());
    CHECK_EQ(arr2[0], 42);
    CHECK_EQ(arr2[1023], 84);
}

TEST_CASE("Ensure BoxArray copies correctly") {
    BoxArray<int, 1024> arr;
    arr[0] = 42;
    arr[1023] = 84;

    auto arr2 = BoxArray(arr);
    CHECK_EQ(arr2[0], 42);
    CHECK_EQ(arr2[1023], 84);

    CHECK_EQ(arr[0], 42);
    CHECK_EQ(arr[1023], 84);
}

TEST_CASE("Ensure BoxArray moves correctly") {
    BoxArray<int, 1024> arr;
    arr[0] = 42;
    arr[1023] = 84;

    auto arr2 = std::move(arr);
    CHECK_EQ(arr2[0], 42);
    CHECK_EQ(arr2[1023], 84);
}
