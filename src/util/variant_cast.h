#pragma once

#include <variant>

/// https://stackoverflow.com/q/47203255
template <class... Args>
struct variant_cast_proxy
{
    std::variant<Args...> && v;

    template <class... ToArgs>
    operator std::variant<ToArgs...>() &&
    {
        return std::visit(
            [](auto&& arg) -> std::variant<ToArgs...> { return std::move(arg) ; },
            std::move(v));
    }
};

template <class... Args>
auto variant_cast(std::variant<Args...> && v) -> variant_cast_proxy<Args...>
{
    return {std::move(v)};
}
