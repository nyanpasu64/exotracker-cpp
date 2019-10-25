#include "audio/event_queue.h"

#include "doctest.h"

namespace EventEnum_ {
enum EventEnum {
    EndOfCallback,
    Test1,
    Test2,
    COUNT,
};
}
using EventEnum_::EventEnum;


TEST_CASE("Test that EventQueue is filled with time=NEVER, instead of 0.") {
    using EventT = EventEnum;
    using PQ = audio::EventQueue<EventT>;
    PQ pq;
    {
        auto event = pq.next_event();
        CHECK(event.event_id == 0);
        CHECK(event.cyc_elapsed == PQ::NEVER);
    }
}


TEST_CASE("Test enqueueing events at t=0.") {
    using EventT = EventEnum;
    using PQ = audio::EventQueue<EventT>;
    PQ pq;

    pq.set_timeout(EventT::EndOfCallback, 10);
    pq.set_timeout(EventT::Test1, 30);

    {
        auto event = pq.next_event();
        CHECK(event.event_id == EventT::EndOfCallback);
        CHECK(event.cyc_elapsed == 10);
    }
    {
        auto event = pq.next_event();
        CHECK(event.event_id == EventT::Test1);
        CHECK(event.cyc_elapsed == 20);
    }
}

TEST_CASE("Test enqueueing events later in time.") {
    using EventT = EventEnum;
    using PQ = audio::EventQueue<EventT>;
    PQ pq;

    pq.set_timeout(EventT::EndOfCallback, 10);
    {
        auto event = pq.next_event();
        CHECK(event.event_id == EventT::EndOfCallback);
        CHECK(event.cyc_elapsed == 10);
    }
    // now == 10
    pq.set_timeout(EventT::Test1, 30);
    {
        auto event = pq.next_event();
        CHECK(event.event_id == EventT::Test1);
        CHECK(event.cyc_elapsed == 30);
    }
    // now == 40
}

TEST_CASE("Test that identically timed events are dequeued in order of increasing EventID.") {
    using EventT = EventEnum;
    using PQ = audio::EventQueue<EventT>;
    PQ pq;

    // Enqueue events out of order.
    pq.set_timeout(EventT::Test2, 10);
    pq.set_timeout(EventT::EndOfCallback, 10);
    pq.set_timeout(EventT::Test1, 10);

    // Assert they're dequeued in increasing order.
    {
        auto event = pq.next_event();
        CHECK(event.event_id == EventT::EndOfCallback);
        CHECK(event.cyc_elapsed == 10);
    }
    {
        auto event = pq.next_event();
        CHECK(event.event_id == EventT::Test1);
        CHECK(event.cyc_elapsed == 0);
    }
    {
        auto event = pq.next_event();
        CHECK(event.event_id == EventT::Test2);
        CHECK(event.cyc_elapsed == 0);
    }
}


enum class EventClass {
    EndOfCallback,
    Test1,
    Test2,
    COUNT,
};

TEST_CASE("Test PQ with an enum class.") {
    using EventT = EventClass;
    using PQ = audio::EventQueue<EventT>;
    PQ pq;

    pq.set_timeout(EventT::EndOfCallback, 10);
    {
        auto event = pq.next_event();
        CHECK(event.event_id == EventT::EndOfCallback);
        CHECK(event.cyc_elapsed == 10);
    }
}
