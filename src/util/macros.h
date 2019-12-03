#pragma once

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
        throw std::logic_error(#expr " is false"); \
    } } while (0)
