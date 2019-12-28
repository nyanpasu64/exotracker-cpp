#pragma once

#include "events.h"
#include "timed_events.h"

#include <immer/flex_vector.hpp>
#include <immer/flex_vector_transient.hpp>

#include <algorithm>  // std::lower_bound
#include <optional>

namespace doc::event_list {

using events::RowEvent;
using timed_events::TimeInPattern;
using timed_events::TimedRowEvent;

/// Pattern type.
using EventList = immer::flex_vector<TimedRowEvent>;

/// Owning wrapper for EventList (I wish C++ had extension methods),
/// adding the ability to binary-search and treat it as a map.
template<typename ImmerT>
struct KV_Internal {
    using This = KV_Internal<ImmerT>;

    ImmerT event_list;

private:
    static bool cmp_less_than(TimedRowEvent const &a, TimeInPattern const &b) {
        return a.time < b;
    }

    bool iter_matches_time(typename ImmerT::iterator iter, TimeInPattern t) const {
        if (iter == event_list.end()) return false;
        if (iter->time != t) return false;
        return true;
    }

public:
    typename ImmerT::iterator greater_equal(TimeInPattern t) const {
        return std::lower_bound(event_list.begin(), event_list.end(), t, cmp_less_than);
    }

    bool contains_time(TimeInPattern t) const {
        // I cannot use std::binary_search,
        // since it requires that the comparator's arguments have interchangable types.
        return iter_matches_time(greater_equal(t), t);
    }

    std::optional<RowEvent> get_maybe(TimeInPattern t) const {
        auto iter = greater_equal(t);
        if (iter_matches_time(iter, t)) {
            return {iter->v};
        } else {
            return {};
        }
    }

    RowEvent get_or_default(TimeInPattern t) const {
        auto iter = greater_equal(t);
        if (iter_matches_time(iter, t)) {
            return iter->v;
        } else {
            return {};
        }
    }

    /// Only works with KV, not transient. This is because .insert() is missing on flex_vector_transient.
    This set_time(TimeInPattern t, RowEvent v) {
        auto iter = greater_equal(t);
        TimedRowEvent timed_v{t, v};
        if (iter_matches_time(iter, t)) {
            return This{event_list.set(iter.index(), timed_v)};
        } else {
            return This{event_list.insert(iter.index(), timed_v)};
        }
    }
};

using KV = KV_Internal<EventList>;
using KVTransient = KV_Internal<EventList::transient_type>;

}
