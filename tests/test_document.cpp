#include "doc.h"
#include "edit_util/kv.h"

#include <map>
#include <type_traits>

#include "doctest.h"

namespace doc {
static std::ostream& operator<< (std::ostream& os, const RowEvent& value) {
    os << "RowEvent{";
    if (value.note.has_value()) {
        Note note = *value.note;
        if (note.is_cut()) {
            os << "note cut";
        } else if (note.is_release()) {
            os << "note release";
        } else if (note.is_valid_note()) {
            os << int(note.value);
        } else {
            os << "invalid note " << int(note.value);
        }
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

TEST_CASE ("Test that EventList and KV search is implemented properly.") {
    using namespace doc;
    using edit_util::kv::KV;

    doc::EventList events;
    events.push_back({{0, 0}, {}});
    events.push_back({{0, 1}, {1}});
    events.push_back({{{1, 3}, 0}, {3}});
    events.push_back({{{2, 3}, 0}, {6}});
    events.push_back({{1, 0}, {10}});
    events.push_back({{2, 0}, {20}});

    CHECK(KV{events}.greater_equal({-1, 0})->time == TimeInPattern{0, 0});
    CHECK(KV{events}.greater_equal({0, 0})->time == TimeInPattern{0, 0});
    CHECK(KV{events}.greater_equal({{1, 2}, 0})->time == TimeInPattern{{2, 3}, 0});
    CHECK(KV{events}.greater_equal({10, 0}) == events.end());
}
