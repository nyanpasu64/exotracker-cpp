#pragma once

#include "events.h"

#include <limits>

namespace doc::timed_events {

/// Why signed?
///
/// - We subtract TickT and expect to get a signed result.
/// - Events can have negative offsets and play before their anchor beat.
///   This is a feature(tm).
///
/// In any case, ticks are soft-restricted to below 1 billion (MAX_TICK), so this
/// shouldn't be an issue in practice.
using TickT = int32_t;

struct TimedRowEvent {
    /// Relative to pattern start. May be offset further through signed Gxx delay
    /// effects.
    TickT anchor_tick;

    events::RowEvent v;

// impl
    TickT tick_offset(events::EffColIndex n_effect_col) const;

    /// Returns event's time relative to pattern begin, including Gxx effects in the
    /// first _ columns.
    TickT time(events::EffColIndex n_effect_col) const {
        return anchor_tick + tick_offset(n_effect_col);
    }

    DEFAULT_EQUALABLE(TimedRowEvent)
};

// end namespace
}
