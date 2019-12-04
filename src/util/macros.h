#pragma once

#include <fmt/core.h>

#include <stdexcept>
#include <cassert>  // assert() is not used in this header, but supplied to includers.

// based off Q_DISABLE_COPY and the like.

#define DISABLE_COPY(Class) \
    Class(const Class &) = delete;\
    Class &operator=(const Class &) = delete;

#define DISABLE_MOVE(Class) \
    Class(Class &&) = delete; \
    Class &operator=(Class &&) = delete;

#define DISABLE_COPY_MOVE(Class) \
    DISABLE_COPY(Class) \
    DISABLE_MOVE(Class)

#define release_assert(expr) \
    do { if (!(expr)) { \
        throw std::logic_error("release_assert failed: `" #expr "` is false"); \
    } } while (0)

#define release_assert_equal(lhs, rhs) \
    do { if ((lhs) != (rhs)) { \
        std::string message = fmt::format( \
            "release_assert failed: `{}`={} != `{}`={}", #lhs, lhs, #rhs, rhs \
        ); \
        throw std::logic_error(message); \
    } } while (0)

#ifdef UNITTEST
#define TEST_PUBLIC public
#else
#define TEST_PUBLIC private
#endif
