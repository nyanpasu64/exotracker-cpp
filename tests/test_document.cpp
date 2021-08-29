#include "doc.h"
#include "doc_util/event_search.h"
#include "doc_util/event_builder.h"

#include <fmt/core.h>

#include <map>
#include <type_traits>

#include <doctest.h>

TEST_CASE("Test that TimeInPattern comparisons work properly.") {

    // In C++, if you aggregate-initialize and not fill in all fields,
    // the remainder are set to {} (acts like 0).
    CHECK(doc::TimeInPattern{}.anchor_beat == 0);

    // std::map<doc::TimeInPattern, doc::RowEvent>
    CHECK(doc::TimeInPattern{0, 0} == doc::TimeInPattern{0, 0});
    CHECK(doc::TimeInPattern{1, 0} == doc::TimeInPattern{1, 0});

    const doc::TimeInPattern &half = doc::TimeInPattern{{1, 2}, 0};
    const doc::TimeInPattern &one = doc::TimeInPattern{1, 0};
    CHECK(half != one);
    CHECK(half < one);
    CHECK_UNARY(half < one || half == one || half > one);

    std::map<doc::BeatFraction, int> fraction_test;
    fraction_test[{1, 2}] = 5;
    fraction_test[1] = 10;
    CHECK(fraction_test.size() == 2);
    CHECK(fraction_test.at({1, 2}) == 5);
}

/// Only test EventSearch const iterators and EventSearchMut mutable iterators,
/// for simplicity.
/// ListT=EventList copies the list unnecessarily, but it's not a big deal.
template<typename SearchT, typename ListT>
void check_beat_search(ListT events) {
    using namespace doc_util::event_builder;
    using Rev = typename ListT::reverse_iterator;

    SearchT kv{events};

    // Ensure "no element found" works, and >= and > match.
    CHECK(kv.beat_begin(-1)->anchor_beat == 0);
    CHECK(kv.beat_end(-1)->anchor_beat == 0);

    // Ensure that in "elements found" mode,
    // >= and > enclose all elements anchored to the beat.
    CHECK(kv.beat_begin(0)->v.note == std::nullopt);
    CHECK(Rev{kv.beat_end(0)}->v.note == 1);
    CHECK(kv.beat_end(0)->anchor_beat == BeatFraction(1, 3));

    // Test "past the end" search.
    CHECK(kv.beat_begin(10) == events.end());
    CHECK(kv.beat_end(10) == events.end());
}


TEST_CASE ("Test that EventList and KV search is implemented properly.") {
    using namespace doc;
    using doc_util::event_search::EventSearch;
    using doc_util::event_search::EventSearchMut;
    using namespace doc_util::event_builder;

    EventList events;
    events.push_back({0, {}});
    events.push_back({0, {1}});
    events.push_back({{1, 3}, {3}});
    events.push_back({{2, 3}, {6}});
    events.push_back({1, {10}});
    events.push_back({2, {20}});

    SUBCASE("Check (beat) search.") {
        check_beat_search<EventSearch, TimedEventsRef>(events);
        check_beat_search<EventSearchMut, EventList>(std::move(events));
    }

    SUBCASE("Test get_or_insert().") {
        EventSearchMut kv{events};
        auto n = events.size();

        // If one event is anchored here, make sure the right event is picked.
        CHECK(kv.get_or_insert(1).anchor_beat == 1);
        CHECK(events.size() == n);

        // If multiple events are anchored here, make sure the last one is picked.
        CHECK(kv.get_or_insert(0).v.note == 1);
        CHECK(events.size() == n);

        // Test inserting events at times not already present.
        auto & added = kv.get_or_insert(-1);
        CHECK(added.anchor_beat == -1);
        CHECK(events.size() == n + 1);
        CHECK(&added == &events[0]);
    }
}
