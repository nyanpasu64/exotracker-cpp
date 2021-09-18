#pragma once

#include <type_traits>

// Based on Q_DECLARE_OPERATORS_FOR_FLAGS.
#define DECLARE_OPERATORS_FOR_FLAGS(Enum) \
    constexpr inline Enum operator|(Enum f1, Enum f2) noexcept { \
        using IntT = std::underlying_type_t<Enum>; \
        return Enum(IntT(f1) | IntT(f2)); \
    }
