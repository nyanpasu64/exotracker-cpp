#include "events.h"
#include "util/compare_impl.h"

namespace doc::events {

EQUALABLE_IMPL(RowEvent, (self.note, self.instr))

}
