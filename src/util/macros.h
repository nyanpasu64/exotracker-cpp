#pragma once


// #define mut (no longer used)
// do NOT #define ref, this will result in mysterious compile errors in #include headers!

#define LOOP(i, n) for (int i = 0; i < n; ++i)
#define LOOPD(i, n, delta) for (int i = 0; i < n; i += delta)
#define LOOPN(i, begin, end) for (int i = begin; i < end; ++i)
