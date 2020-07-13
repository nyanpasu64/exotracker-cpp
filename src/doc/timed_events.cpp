#include "timed_events.h"
#include "util/math.h"

namespace doc::timed_events {

FractionInt round_to_int(BeatFraction v) {
    v += BeatFraction{1, 2};
    return util::math::frac_floor(v);
}

}
