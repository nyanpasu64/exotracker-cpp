#pragma once

#include <array>
#include <cstddef>

/// EnumT can be an enum or enum class. It must define a COUNT element.
template<typename EnumT>
size_t constexpr enum_count = (size_t) EnumT::COUNT;

/// Allocation-free map from an EnumT to a ValueT.
///
/// Effectively identical to a std::array, but can be indexed by EnumT directly.
/// This is useful if EnumT is an enum class (cannot be implicitly converted to int).
///
/// EnumT can be an enum or enum class. It must define a COUNT element.
template<typename EnumT, typename ValueT>
class EnumMap : public std::array<ValueT, enum_count<EnumT>> {
    using super = std::array<ValueT, enum_count<EnumT>>;

public:
    using super::super;

    typename super::reference operator[] (typename super::size_type n) {
        return super::operator[](n);
    }
    typename super::const_reference operator[] (typename super::size_type n) const {
        return super::operator[](n);
    }

    typename super::reference operator[] (EnumT n) {
        return super::operator[]((typename super::size_type) n);
    }
    typename super::const_reference operator[] (EnumT n) const {
        return super::operator[]((typename super::size_type) n);
    }
};
