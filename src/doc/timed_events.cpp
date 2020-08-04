#include "timed_events.h"
#include "util/compare_impl.h"
#include "util/math.h"

namespace doc::timed_events {

/*
0CC uses repeated bisection to compute grooves.
For example, if you type 65 into the groove calculator and expand 3 times,
you get gaps of 9 8 8 8 8 8 8 8.
If you split 69 into 8 parts, you get a groove of 9 9 9 8 9 8 9 8.
This cannot be replicated through a simple fraction-rounding strategy.

0CC grooves are highly structured, has a regular pattern,
and don't generalize to non-powers-of-2.

Do people like 0CC grooves better than normal note rounding?
Is it more "musical" to place long delays first and short delays last,
and produce a "swing" sound instead of reverse-swing?
Or is it imperceptible?

0CC's groove calculator probably has more total error,
but it ensures that every subdivision has a long gap before a short gap.

In any case, I decided to use ceil to round off fractions.
This simulates this swing to some extent, but doesn't match 0CC in subdivisions.
*/
FractionInt round_to_int(BeatFraction v) {
    return util::math::frac_ceil(v);
}

COMPARABLE_IMPL(TimeInPattern, (self.anchor_beat, self.tick_offset))

}
