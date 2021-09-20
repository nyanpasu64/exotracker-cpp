/// Do not include in headers! Only in implementation files!
/// tuple and std::tie slow down compilation!

#pragma once

#include <tuple>  // std::tie


#define ZZ_KEY_FUNCTION(T, FUNCTION, PAREN_OF_FIELDS) \
    static auto FUNCTION(T const& self) { \
        return std::tie PAREN_OF_FIELDS; \
    } \


#define ZZ_COMPARE_IMPL(T, FUNCTION) \
    [[nodiscard]] bool T::operator<(T const& other) const { \
        return FUNCTION(*this) < FUNCTION(other); \
    } \
    [[nodiscard]] bool T::operator>(T const& other) const { \
        return FUNCTION(*this) > FUNCTION(other); \
    } \
    [[nodiscard]] bool T::operator>=(T const& other) const { \
        return FUNCTION(*this) >= FUNCTION(other); \
    } \
    [[nodiscard]] bool T::operator<=(T const& other) const { \
        return FUNCTION(*this) <= FUNCTION(other); \
    } \


#define ZZ_COMPARE_INTERNAL(T, FUNCTION) \
    [[nodiscard]] bool T::operator<(T const& other) const { \
        return FUNCTION(*this) < FUNCTION(other); \
    } \
    [[nodiscard]] bool T::operator>(T const& other) const { \
        return FUNCTION(*this) > FUNCTION(other); \
    } \
    [[nodiscard]] bool T::operator>=(T const& other) const { \
        return FUNCTION(*this) >= FUNCTION(other); \
    } \
    [[nodiscard]] bool T::operator<=(T const& other) const { \
        return FUNCTION(*this) <= FUNCTION(other); \
    } \


#define ZZ_EQUAL_INTERNAL(T, FUNCTION) \
    [[nodiscard]] bool T::operator==(T const& other) const { \
        return FUNCTION(*this) == FUNCTION(other); \
    } \
    [[nodiscard]] bool T::operator!=(T const& other) const { \
        return FUNCTION(*this) != FUNCTION(other); \
    } \


#define COMPARE_ONLY_IMPL(T, PAREN_OF_FIELDS) \
    ZZ_KEY_FUNCTION(T, compare_only_, PAREN_OF_FIELDS) \
    ZZ_COMPARE_INTERNAL(T, compare_only_)

#define EQUALABLE_IMPL(T, PAREN_OF_FIELDS) \
    ZZ_KEY_FUNCTION(T, equalable_, PAREN_OF_FIELDS) \
    ZZ_EQUAL_INTERNAL(T, equalable_)

#define COMPARABLE_IMPL(T, PAREN_OF_FIELDS) \
    ZZ_KEY_FUNCTION(T, comparable_, PAREN_OF_FIELDS)\
    ZZ_EQUAL_INTERNAL(T, comparable_) \
    ZZ_COMPARE_INTERNAL(T, comparable_)
