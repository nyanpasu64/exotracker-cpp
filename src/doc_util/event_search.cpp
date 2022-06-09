#include "event_search.h"

#include <algorithm>  // std::lower_bound
#include <type_traits>

namespace doc_util::event_search {

static TickT const & time(const TimedRowEvent & a) {
    return a.anchor_tick;
}

template<auto key_func>
class KeyedCmp {
    using KeyFunc = decltype(key_func);
    using KeyType = typename std::invoke_result_t<KeyFunc, const TimedRowEvent &>;

public:
    // i wish <algorithm> accepted a key function instead of a comparison.
    bool operator()(const TimedRowEvent & a, const KeyType & b) {
        return key_func(a) < b;
    }

    bool operator()(const KeyType & a, const TimedRowEvent & b) {
        return a < key_func(b);
    }
};

#define IMPL_IMPL(THIS, FUNC_NAME, BOUND, KEY_T, CONST, ITERATOR, KEY_FUNC) \
    THIS::ITERATOR THIS::FUNC_NAME(KEY_T t) CONST { \
        return BOUND( \
            _event_list.begin(), _event_list.end(), t, KeyedCmp<KEY_FUNC>{} \
        ); \
    } \

using Const = EventSearch;
using Mut = EventSearchMut;


#define IMPL(FUNC_NAME, BOUND, KEY_T, KEY_FUNC) \
    IMPL_IMPL(Const, FUNC_NAME, BOUND, KEY_T, const, ConstIterator, KEY_FUNC) \
    IMPL_IMPL(Mut, FUNC_NAME, BOUND, KEY_T, const, ConstIterator, KEY_FUNC) \
    IMPL_IMPL(Mut, FUNC_NAME, BOUND, KEY_T, , Iterator, KEY_FUNC) \

IMPL(tick_begin, std::lower_bound, TickT, time)
IMPL(tick_end, std::upper_bound, TickT, time)

TimedRowEvent * EventSearchMut::get_maybe(TickT beat) {
    // Last event anchored to this beat fraction.
    EventList::reverse_iterator it{tick_end(beat)};

    if (it != _event_list.rend() && it->anchor_tick == beat) {
        return &*it;
    } else {
        return nullptr;
    }
}

TimedRowEvent & EventSearchMut::get_or_insert(TickT beat) {
    // Last event anchored to this beat fraction.
    EventList::reverse_iterator it{tick_end(beat)};

    if (it != _event_list.rend() && it->anchor_tick == beat) {
        return *it;
    } else {
        TimedRowEvent ev {
            .anchor_tick = beat,
            .v = RowEvent{},
        };
        return *_event_list.insert(it.base(), std::move(ev));
    }
}

}
