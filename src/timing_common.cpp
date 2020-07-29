#include "timing_common.h"
#include "util/compare_impl.h"

namespace timing {

COMPARABLE_IMPL(PatternAndBeat, (self.seq_entry_index, self.beat))

EQUALABLE_IMPL(
    SequencerTime,
    (self.seq_entry_index, self.curr_ticks_per_beat, self.beats, self.ticks)
)

}
