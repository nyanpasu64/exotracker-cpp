#include "doc.h"
#include "edit_util/kv.h"
#include "edit_util/shorthand.h"

#include <fmt/core.h>

#include <map>
#include <type_traits>

#include "doctest.h"

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

TEST_CASE ("Test that EventList and KV search is implemented properly.") {
    using namespace doc;
    using edit_util::kv::KV;
    using namespace edit_util::shorthand;

    doc::EventList events;
    events.push_back({at(0), {}});
    events.push_back({at_delay(0, 1), {1}});
    events.push_back({at({1, 3}), {3}});
    events.push_back({at({2, 3}), {6}});
    events.push_back({at(1), {10}});
    events.push_back({at(2), {20}});

    using Rev = doc::EventList::reverse_iterator;

    KV kv{events};

    SUBCASE("Check (beat, tick) search.") {
        // Ensure "no element found" works, and >= and > match.
        CHECK(kv.greater_equal(at(-1))->time == at(0));
        CHECK(kv.greater(at(-1))->time == at(0));

        CHECK(kv.greater_equal(at({1, 2}))->time == at({2, 3}));
        CHECK(kv.greater(at({1, 2}))->time == at({2, 3}));

        // Ensure that in "element found", >= and > enclose one element.
        CHECK(kv.greater_equal(at(0))->time == at(0));
        CHECK(Rev{kv.greater(at(0))}->time == at(0));
        CHECK(kv.greater(at(0))->time == at_delay(0, 1));

        CHECK(kv.greater_equal(at(1))->time == at(1));
        CHECK(Rev{kv.greater(at(1))}->time == at(1));
        CHECK(kv.greater(at(1))->time == at(2));

        // Test "past the end" search.
        CHECK(kv.greater_equal(at(10)) == events.end());
        CHECK(kv.greater(at(10)) == events.end());
    }

    SUBCASE("Check (beat) search.") {
        // Ensure "no element found" works, and >= and > match.
        CHECK(kv.beat_begin(-1)->time == at(0));
        CHECK(kv.beat_end(-1)->time == at(0));

        // Ensure that in "elements found" mode,
        // >= and > enclose all elements anchored to the beat.
        CHECK(kv.beat_begin(0)->time == at(0));
        CHECK(Rev{kv.beat_end(0)}->time == at_delay(0, 1));
        CHECK(kv.beat_end(0)->time == at({1, 3}));

        // Test "past the end" search.
        CHECK(kv.beat_begin(10) == events.end());
        CHECK(kv.beat_end(10) == events.end());
    }

    SUBCASE("Test get_or_insert().") {
        auto n = events.size();

        // If one event is anchored here, make sure the right event is picked.
        CHECK(kv.get_or_insert(1).time == at(1));
        CHECK(events.size() == n);

        // If multiple events are anchored here, make sure the last one is picked.
        CHECK(kv.get_or_insert(0).time == at_delay(0, 1));
        CHECK(events.size() == n);

        // Test inserting events at times not already present.
        auto & added = kv.get_or_insert(-1);
        CHECK(added.time == at(-1));
        CHECK(events.size() == n + 1);
        CHECK(&added == &events[0]);
    }
}
