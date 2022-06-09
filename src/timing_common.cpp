#include "timing_common.h"
#include "util/compare_impl.h"

namespace timing {

EQUALABLE_IMPL(SequencerTime, (self.ticks, self.playing))

}
