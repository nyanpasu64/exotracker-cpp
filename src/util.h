#pragma once

#include "util/macros.h"

namespace util {

template<typename T>
static inline T distance(T a, T b) {
    return b - a;
}

}
