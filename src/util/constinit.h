#pragma once

#if !(__cpp_constinit)
// The constinit keyword acts like an attribute in that it verifies that the data is
// initialized statically rather than at runtime (before main()).
// It doesn't actually affect the behavior of a program, whereas constexpr does.
// So on compilers that don't support constinit (primarily MSVC as of 2021-04),
// we can define it as a no-op keyword.
// This eliminates the sanity check for people building on MSVC, but this is acceptable
// since I primarily build on Clang 12 which supports constinit,
// and CI uses Clang as well.
#define constinit
#endif
