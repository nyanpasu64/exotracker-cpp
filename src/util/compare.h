/// Comparisons are not constexpr because they're defined elsewhere.

#pragma once

#define COMPARE_ONLY(T) \
    [[nodiscard]] bool operator<(T const& other) const; \
    [[nodiscard]] bool operator>(T const& other) const; \
    [[nodiscard]] bool operator>=(T const& other) const; \
    [[nodiscard]] bool operator<=(T const& other) const;

#define EQUALABLE(T) \
    [[nodiscard]] bool operator==(T const& other) const; \
    [[nodiscard]] bool operator!=(T const& other) const;

#define COMPARABLE(T) \
    COMPARE_ONLY(T) \
    EQUALABLE(T)


/// Constexpr unlike EQUALABLE.
#define EQUALABLE_CONSTEXPR(T, FIELD) \
    [[nodiscard]] constexpr bool operator==(T const& other) const { \
        return this->FIELD == other.FIELD; \
    } \
    [[nodiscard]] constexpr bool operator!=(T const& other) const { \
        return this->FIELD != other.FIELD; \
    } \

#define EQUALABLE_SIMPLE(T, FIELD) \
    [[nodiscard]] bool operator==(T const& other) const { \
        return this->FIELD == other.FIELD; \
    } \
    [[nodiscard]] bool operator!=(T const& other) const { \
        return this->FIELD != other.FIELD; \
    } \

/// Requires #include <compare>
#define DEFAULT_COMPARE(T) \
    auto operator<=>(T const&) const = default;

/// Does not require #include <compare>
#define DEFAULT_EQUALABLE(T) \
    bool operator==(T const&) const = default;
