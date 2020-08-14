#pragma once

#define TYPEOF(VAR)  std::remove_reference_t<decltype(VAR)>

#define COUNT_UP(TYPE, VAR, BEGIN, END_EXCL) \
    for (TYPE (VAR) = TYPEOF(VAR)(BEGIN); size_t(VAR) < size_t(END_EXCL); ++(VAR))

/// It is impressively difficult to make an unsigned value count backwards
/// from n-1 through 0, but no further.
#define COUNT_DOWN(TYPE, VAR, BEGIN_EXCL, END) \
    for (TYPE (VAR) = TYPEOF(VAR)(BEGIN_EXCL - 1); ptrdiff_t(VAR + 1) > ptrdiff_t(END); --(VAR))
