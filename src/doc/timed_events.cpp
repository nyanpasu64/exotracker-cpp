#include "timed_events.h"
#include "doc/effect_names.h"
#include "util/enumerate.h"
#include "util/math.h"

#include <cstdint>

namespace doc::timed_events {

using namespace events;
namespace effs = effect_names;

TickT TimedRowEvent::tick_offset(EffColIndex n_effect_col) const {
    for (EffColIndex i = 0; i < n_effect_col; i++) {
        MaybeEffect const& e = this->v.effects[i];
        if (e && e->name == effs::DELAY) {
            auto v = (TickT) e->value;
            return (v & 0x80) ? -(v - 0x80) : v;
        }
    }

    return 0;
}

}
