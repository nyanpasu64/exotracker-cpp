#pragma once

#include <type_traits>


#define SAFE_TYPEDEF_IMPL(IntT, Wrap)                                      \
    IntT v;                                                        \
    constexpr Wrap() = default;                                              \
    constexpr Wrap(const Wrap & v_) = default;                                  \
    constexpr Wrap(Wrap&&) = default;                                           \
    constexpr Wrap & operator=(const Wrap & rhs) = default;                     \
    constexpr Wrap & operator=(Wrap&&) = default;                               \
    constexpr operator IntT & () { return v; }                               \
    constexpr operator IntT const& () const { return v; }                               \
    constexpr auto operator<=>(Wrap const& rhs) const = default;


#define SAFE_TYPEDEF(IntT, Wrap) \
    SAFE_TYPEDEF_IMPL(IntT, Wrap) \
    template<typename OtherInt> \
    constexpr Wrap(OtherInt v_) : v((IntT) v_) {};

#define EXPLICIT_TYPEDEF(IntT, Wrap) \
    SAFE_TYPEDEF_IMPL(IntT, Wrap)\
    template<typename OtherInt> \
    explicit constexpr Wrap(OtherInt v_) : v((IntT) v_) {}; \
    constexpr Wrap(std::make_signed_t<IntT> v_) : v((IntT) v_) {}; \
    constexpr Wrap(std::make_unsigned_t<IntT> v_) : v((IntT) v_) {};
