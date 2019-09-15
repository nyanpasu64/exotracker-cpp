#include "document.h"

//#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

namespace doc = document;

namespace document {
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

TEST_CASE ("Test that ChannelEvents/std::map<TimeInPattern, ...> treats different timestamps differently.") {

    // std::map<document::TimeInPattern, document::RowEvent>
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

    doc::ChannelEvents events;
    events[{0, 0}] = doc::RowEvent{{}};
    events[{0, 1}] = doc::RowEvent{1};
    events[{{1, 3}, 0}] = doc::RowEvent{3};
    events[{{2, 3}, 0}] = doc::RowEvent{6};
    events[{1, 0}] = doc::RowEvent{10};
    events[{2, 0}] = doc::RowEvent{20};

    CHECK(events.size() == 6);
    CHECK(events[{0, 0}] == doc::RowEvent{{}});
    CHECK(events[{0, 1}] == doc::RowEvent{1});
    CHECK(events[{{1, 3}, 0}] == doc::RowEvent{3});
    CHECK(events[{{2, 3}, 0}] == doc::RowEvent{6});
    CHECK(events[{1, 0}] == doc::RowEvent{10});
    CHECK(events[{2, 0}] == doc::RowEvent{20});
}
