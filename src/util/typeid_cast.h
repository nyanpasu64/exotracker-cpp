#pragma once

#include <type_traits>
#include <typeinfo>

// Taken from https://github.com/ClickHouse/ClickHouse/blob/master/src/Common/typeid_cast.h

/** Checks type by comparing typeid.
  * The exact match of the type is checked. That is, cast to the ancestor will be unsuccessful.
  * In the rest, behaves like a dynamic_cast.
  */
template <typename To, typename From>
auto typeid_cast(From * from) -> std::enable_if_t<std::is_pointer_v<To>, To> {
    if (
        // static check
        (typeid(From) == typeid(std::remove_pointer_t<To>))
        // runtime check
        || (typeid(*from) == typeid(std::remove_pointer_t<To>))
    )
        return static_cast<To>(from);
    else
        return nullptr;
}
