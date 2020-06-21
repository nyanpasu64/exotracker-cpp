#pragma once

#include <cstring>
#include <type_traits>

// https://en.cppreference.com/w/cpp/numeric/bit_cast
template <class To, class From>
typename std::enable_if<
    (sizeof(To) == sizeof(From)) &&
    std::is_trivially_copyable<From>::value &&
    std::is_trivial<To>::value &&
    (std::is_copy_constructible<To>::value || std::is_move_constructible<To>::value),
    // this implementation requires that To is trivially default constructible
    // and that To is copy constructible or move constructible.
    To>::type
// constexpr support needs compiler magic
bit_cast(const From &src) noexcept
{
    To dst;
    std::memcpy(&dst, &src, sizeof(To));
    return dst;
}
