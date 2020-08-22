#pragma once

#include "modified_common.h"

namespace edit::modified {

enum ModifiedFlags : ModifiedInt {
    TimelineRows = 0x1,
    Patterns = 0x2,
    Tempo = 0x4,
};

}
