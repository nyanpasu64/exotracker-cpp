#include "kv.h"

#include <algorithm>  // std::lower_bound
#include <type_traits>

namespace edit_util::kv {

static TimeInPattern const & time_and_offset(const TimedRowEvent & a) {
    return a.time;
}

static BeatFraction const & time(const TimedRowEvent & a) {
    return a.time.anchor_beat;
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

#define IMPL(FUNC_NAME, BOUND, KEY_T, KEY_FUNC) \
    KV::ConstIterator KV::FUNC_NAME(KEY_T t) const { \
        return BOUND( \
            _event_list.begin(), _event_list.end(), t, KeyedCmp<KEY_FUNC>{} \
        ); \
    } \
    KV::Iterator KV::FUNC_NAME(KEY_T t) { \
        return BOUND( \
            _event_list.begin(), _event_list.end(), t, KeyedCmp<KEY_FUNC>{} \
        ); \
    } \

IMPL(greater_equal, std::lower_bound, TimeInPattern, time_and_offset)
IMPL(greater, std::upper_bound, TimeInPattern, time_and_offset)

IMPL(beat_begin, std::lower_bound, BeatFraction, time)
IMPL(beat_end, std::upper_bound, BeatFraction, time)

}
