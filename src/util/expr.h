#pragma once

/// Shorthand for an immediately-invoked function expression,
/// intended to look more like GCC's statement expressions.
/// Unfortunately does not allow for returning from the parent function,
/// so you cannot write a try!() macro using these.
///
/// Usage:
///
/// printf("%d\n", EXPR(
///     int x = 0;
///     x++;
///     return x;
/// ));
#define EXPR(...) ([&] { __VA_ARGS__ }())
