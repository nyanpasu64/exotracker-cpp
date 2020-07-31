#pragma once

#include "timing_common.h"
#include "util/compare.h"

#include <compare>
#include <cstdint>

namespace gui::cursor {

using ColumnIndex = uint32_t;
using SubColumnIndex = uint32_t;

struct CursorX {
    ColumnIndex column = 0;
    SubColumnIndex subcolumn = 0;

    DEFAULT_COMPARE(CursorX)
};

struct Cursor {
    CursorX x;
    timing::PatternAndBeat y;
};

}
