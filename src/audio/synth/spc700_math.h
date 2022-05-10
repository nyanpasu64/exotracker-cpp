#pragma once

#include <cstddef>
#include <cstdint>

#define REG(x)  ((size_t) (x))

/// Equivalent to SPC700 `mul ya` followed by discarding a and keeping y.
static inline uint8_t mul_hi(uint8_t a, uint8_t b) {
    return (uint8_t) ((REG(a) * REG(b)) >> 8);
}
