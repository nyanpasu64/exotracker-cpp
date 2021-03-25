#pragma once

#define TYPEOF(VAR)  std::remove_reference_t<decltype(VAR)>

#define COUNT_UP(TYPE, VAR, BEGIN, END_EXCL) \
    for (auto VAR = (TYPE)(BEGIN); size_t(VAR) < size_t(END_EXCL); ++(VAR))

/// An unintuitive "clever" trick to make an unsigned value count backwards
/// from n-1 through 0, but no further.
#define COUNT_DOWN(TYPE, VAR, BEGIN_EXCL, END) \
    for (auto VAR = (TYPE)(BEGIN_EXCL); (--VAR) != (END); )
