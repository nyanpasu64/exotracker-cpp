#pragma once

#include "doc/event_list.h"

#include <algorithm>  // std::lower_bound
#include <optional>

namespace doc::kv {

using namespace doc::event_list;

/// Mutable-reference wrapper for EventList (I wish C++ had extension methods),
/// adding the ability to binary-search and treat it as a map.
struct KV {
    using This = KV;
    using Iterator = EventList::iterator;
    using ConstIterator = EventList::const_iterator;

    EventList & event_list;

private:
    static bool cmp_less_than(TimedRowEvent const &a, TimeInPattern const &b) {
        return a.time < b;
    }

    bool iter_matches_time(ConstIterator iter, TimeInPattern t) const {
        if (iter == event_list.end()) return false;
        if (iter->time != t) return false;
        return true;
    }

public:
    ConstIterator greater_equal(TimeInPattern t) const {
        return std::lower_bound(event_list.begin(), event_list.end(), t, cmp_less_than);
    }

    Iterator greater_equal(TimeInPattern t) {
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

    This & set_time(TimeInPattern t, RowEvent v) {
        auto iter = greater_equal(t);
        TimedRowEvent timed_v{t, v};
        if (iter_matches_time(iter, t)) {
            *iter = timed_v;
        } else {
            event_list.insert(iter, timed_v);
        }
        return *this;
    }
};

}
