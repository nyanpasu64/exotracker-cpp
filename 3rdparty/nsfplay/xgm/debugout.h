#ifndef _DEBUGOUT_H_
#define _DEBUGOUT_H_

#ifdef _MSC_VER

// windows.h is evil.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX

#include <crtdbg.h>
#define _CRTDBG_MAP_ALLOC
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

namespace xgm
{
  class __DebugOut
  {
  public:
    static void printf (const char *format, ...)
    {
      static char buf[1024];
      va_list argl;

        va_start (argl, format);
        _vsnprintf (buf, 1024, format, argl);
        OutputDebugStringA (buf);
        va_end (argl);
    }
  };
#define DEBUG_OUT __DebugOut::printf
}

#else
#define DEBUG_OUT printf
#endif

#endif
