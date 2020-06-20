#pragma once

#include <cstdlib>  // div()

namespace util::math {

template<typename IntT>
inline IntT modulo(IntT num, IntT den) {
    return (num % den + den) % den;
}

template<typename IntT>
inline void inplace_modulo(IntT & num, IntT den) {
    num = (num % den + den) % den;
}

template<typename IntT>
inline IntT floordiv(IntT a, IntT b) {
    // Adapted from https://stackoverflow.com/a/39304947
    auto [quot, rem] = div(a, b);
    if (rem != 0 && ((rem < 0) != (b < 0))) {
        quot -= 1;
    }
    return quot;
}

template<typename IntT>
inline IntT ceildiv(IntT a, IntT b) {
    return -floordiv(-a, b);
}

template<typename IntT>
inline IntT floordiv2(IntT num, IntT den) {
    return (num - modulo(num, den)) / den;
}

template<typename RationalT>
inline typename RationalT::int_type frac_floor(RationalT x) {
    return floordiv(x.numerator(), x.denominator());
}

template<typename RationalT>
inline typename RationalT::int_type frac_ceil(RationalT x) {
    return ceildiv(x.numerator(), x.denominator());
}

}
