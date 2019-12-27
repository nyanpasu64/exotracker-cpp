#pragma once

#include <tuple>  // std::tie

// Adapted from https://www.fluentcpp.com/2019/04/09/how-to-emulate-the-spaceship-operator-before-c20-with-crtp/

// I'd call this _KEY, but _[Uppercase] names are reserved by the compiler.
#define ZZ_KEY(method, paren_of_fields) \
    constexpr auto method() const { \
        return std::tie paren_of_fields; \
    } \

#define ZZ_COMPARE_INTERNAL(method, T) \
    [[nodiscard]] constexpr bool operator<(const T& other) const { \
        return this->method() < other.method(); \
    } \
    [[nodiscard]] constexpr bool operator>(const T& other) const { \
        return this->method() > other.method(); \
    } \
    [[nodiscard]] constexpr bool operator>=(const T& other) const { \
        return this->method() >= other.method(); \
    } \
    [[nodiscard]] constexpr bool operator<=(const T& other) const { \
        return this->method() <= other.method(); \
    } \

#define ZZ_EQUAL_INTERNAL(method, T) \
    [[nodiscard]] constexpr bool operator==(const T& other) const { \
        return this->method() == other.method(); \
    } \
    [[nodiscard]] constexpr bool operator!=(const T& other) const { \
        return this->method() != other.method(); \
    } \

#define COMPARE_ONLY(T, paren_of_fields) \
    ZZ_KEY(compare_only_, paren_of_fields) \
    ZZ_COMPARE_INTERNAL(compare_only_, T)

#define EQUALABLE(T, paren_of_fields) \
    ZZ_KEY(equalable_, paren_of_fields) \
    ZZ_EQUAL_INTERNAL(equalable_, T)

#define COMPARABLE(T, paren_of_fields) \
    ZZ_KEY(comparable_, paren_of_fields)\
    ZZ_EQUAL_INTERNAL(comparable_, T) \
    ZZ_COMPARE_INTERNAL(comparable_, T)
