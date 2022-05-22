#include "doc.h"
#include "doc_util/event_search.h"
#include "doc_util/event_builder.h"

#include <fmt/core.h>

#include <map>
#include <type_traits>

#include <doctest.h>

/// Only test EventSearch const iterators and EventSearchMut mutable iterators,
/// for simplicity.
/// ListT=EventList copies the list unnecessarily, but it's not a big deal.
template<typename SearchT, typename ListT>
void check_tick_search(ListT events) {
    using namespace doc_util::event_builder;
    using Rev = typename ListT::reverse_iterator;

    SearchT kv{events};

    // Ensure "no element found" works, and >= and > match.
    CHECK(kv.tick_begin(-1)->anchor_tick == 0);
    CHECK(kv.tick_end(-1)->anchor_tick == 0);

    // Ensure that in "elements found" mode,
    // >= and > enclose all elements anchored to the beat.
    CHECK(kv.tick_begin(0)->v.note == std::nullopt);
    CHECK(Rev{kv.tick_end(0)}->v.note == 1);
    CHECK(kv.tick_end(0)->anchor_tick == 16);

    // Test "past the end" search.
    CHECK(kv.tick_begin(480) == events.end());
    CHECK(kv.tick_end(480) == events.end());
}


TEST_CASE ("Test that EventList and KV search is implemented properly.") {
    using namespace doc;
    using doc_util::event_search::EventSearch;
    using doc_util::event_search::EventSearchMut;
    using namespace doc_util::event_builder;

    EventList events;
    events.push_back({0, {}});
    events.push_back({0, {1}});
    events.push_back({16, {3}});
    events.push_back({32, {6}});
    events.push_back({48, {10}});
    events.push_back({96, {20}});

    SUBCASE("Check (beat) search.") {
        check_tick_search<EventSearch, TimedEventsRef>(events);
        check_tick_search<EventSearchMut, EventList>(std::move(events));
    }

    SUBCASE("Test get_or_insert().") {
        EventSearchMut kv{events};
        auto n = events.size();

        // If one event is anchored here, make sure the right event is picked.
        CHECK(kv.get_or_insert(16).anchor_tick == 16);
        CHECK(events.size() == n);

        // If multiple events are anchored here, make sure the last one is picked.
        CHECK(kv.get_or_insert(0).v.note == 1);
        CHECK(events.size() == n);

        // Test inserting events at times not already present.
        auto & added = kv.get_or_insert(-1);
        CHECK(added.anchor_tick == -1);
        CHECK(events.size() == n + 1);
        CHECK(&added == &events[0]);
    }
}
