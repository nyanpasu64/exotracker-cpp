#include "timing_common.h"
#include "util/compare_impl.h"

namespace timing {

COMPARABLE_IMPL(GridBlockBeat, (self.grid, self.block, self.beat))
COMPARABLE_IMPL(GridAndBlock, (self.grid, self.block))
COMPARABLE_IMPL(GridAndBeat, (self.grid, self.beat))

EQUALABLE_IMPL(
    SequencerTime,
    (self.grid, self.curr_ticks_per_beat, self.beats, self.ticks)
)

}
