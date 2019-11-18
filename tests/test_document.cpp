#include "document.h"

#include <map>

//#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

namespace doc {
std::ostream& operator<< (std::ostream& os, const RowEvent& value) {
    os << "RowEvent{";
    if (value.note.has_value()) {
        os << int(*value.note);
    }  else {
        os << "{}";
    }
    os << "}";
    return os;
}
}

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

TEST_CASE ("Test that ChannelEvents and KV search is implemented properly.") {
    using namespace doc;

    doc::ChannelEvents events = doc::ChannelEvents()
            .push_back({{0, 0}, {}})
            .push_back({{0, 1}, {1}})
            .push_back({{{1, 3}, 0}, {3}})
            .push_back({{{2, 3}, 0}, {6}})
            .push_back({{1, 0}, {10}})
            .push_back({{2, 0}, {20}});

    CHECK(events.size() == 6);

    // CHECK_UNARY provides much better compiler errors than CHECK.
#undef CHECK
#define CHECK CHECK_UNARY
    CHECK(KV{events}.greater_equal({-1, 0})->time == TimeInPattern{0, 0});
    CHECK(KV{events}.greater_equal({0, 0})->time == TimeInPattern{0, 0});
    CHECK(KV{events}.greater_equal({{1, 2}, 0})->time == TimeInPattern{{2, 3}, 0});
    CHECK(KV{events}.greater_equal({10, 0}) == events.end());

    CHECK(KV{events}.contains_time({0, 0}) == true);
    CHECK(KV{events}.contains_time({0, 1}) == true);
    CHECK(KV{events}.contains_time({{1, 3}, 0}) == true);
    CHECK(KV{events}.contains_time({{2, 3}, 0}) == true);
    CHECK(KV{events}.contains_time({1, 0}) == true);
    CHECK(KV{events}.contains_time({2, 0}) == true);
    CHECK(KV{events}.contains_time({-1, 0}) == false);
    CHECK(KV{events}.contains_time({{1, 2}, 0}) == false);
    CHECK(KV{events}.contains_time({10, 0}) == false);

    CHECK(KV{events}.get_maybe({0, 0}) == doc::RowEvent{{}});
    CHECK(KV{events}.get_maybe({0, 1}) == doc::RowEvent{1});
    CHECK(KV{events}.get_maybe({-1, 0}) == std::nullopt);
    CHECK(KV{events}.get_maybe({{1, 2}, 0}) == std::nullopt);
    CHECK(KV{events}.get_maybe({10, 0}) == std::nullopt);

    CHECK(KV{events}.get_or_default({0, 0}) == doc::RowEvent{{}});
    CHECK(KV{events}.get_or_default({0, 1}) == doc::RowEvent{1});
    CHECK(KV{events}.get_or_default({-1, 0}) == doc::RowEvent{{}});
    CHECK(KV{events}.get_or_default({{1, 2}, 0}) == doc::RowEvent{{}});
    CHECK(KV{events}.get_or_default({10, 0}) == doc::RowEvent{{}});
}
