#include "audio/event_queue.h"

#include "doctest.h"

namespace Event_ {
enum Event {
    EndOfCallback,
    Test1,
    Test2,
    COUNT,
};
}
using Event_::Event;


TEST_CASE("Test that EventQueue is filled with time=NEVER, instead of 0.") {
    using PQ = audio::EventQueue<Event>;
    PQ pq;
    {
        auto event = pq.next_event();
        CHECK(event.event_id == 0);
        CHECK(event.cyc_elapsed == PQ::NEVER);
    }
}


TEST_CASE("Test enqueueing events at t=0.") {
    using PQ = audio::EventQueue<Event>;
    PQ pq;

    pq.set_timeout(Event::EndOfCallback, 10);
    pq.set_timeout(Event::Test1, 30);

    {
        auto event = pq.next_event();
        CHECK(event.event_id == Event::EndOfCallback);
        CHECK(event.cyc_elapsed == 10);
    }
    {
        auto event = pq.next_event();
        CHECK(event.event_id == Event::Test1);
        CHECK(event.cyc_elapsed == 20);
    }
}

TEST_CASE("Test enqueueing events at t=0 with reset_now().") {
    using PQ = audio::EventQueue<Event>;
    PQ pq;

    pq.reset_now();
    pq.set_timeout(Event::EndOfCallback, 10);
    pq.set_timeout(Event::Test1, 30);

    {
        auto event = pq.next_event();
        CHECK(event.event_id == Event::EndOfCallback);
        CHECK(event.cyc_elapsed == 10);
    }
    pq.reset_now();
    pq.reset_now();  // This method should be idempotent.
    {
        auto event = pq.next_event();
        CHECK(event.event_id == Event::Test1);
        CHECK(event.cyc_elapsed == 20);
    }
}


TEST_CASE("Test enqueueing events later in time.") {
    using PQ = audio::EventQueue<Event>;
    PQ pq;

    pq.set_timeout(Event::EndOfCallback, 10);
    {
        auto event = pq.next_event();
        CHECK(event.event_id == Event::EndOfCallback);
        CHECK(event.cyc_elapsed == 10);
    }
    // now == 10
    pq.set_timeout(Event::Test1, 30);
    {
        auto event = pq.next_event();
        CHECK(event.event_id == Event::Test1);
        CHECK(event.cyc_elapsed == 30);
    }
    // now == 40
}

TEST_CASE("Test enqueueing events later in time with reset_now().") {
    using PQ = audio::EventQueue<Event>;
    PQ pq;

    pq.reset_now();
    pq.set_timeout(Event::EndOfCallback, 10);
    {
        auto event = pq.next_event();
        CHECK(event.event_id == Event::EndOfCallback);
        CHECK(event.cyc_elapsed == 10);
    }

    pq.reset_now();
    pq.set_timeout(Event::Test1, 30);
    {
        auto event = pq.next_event();
        CHECK(event.event_id == Event::Test1);
        CHECK(event.cyc_elapsed == 30);
    }
}


TEST_CASE("Test that identically timed events are dequeued in order of increasing EventID.") {
    using PQ = audio::EventQueue<Event>;
    PQ pq;

    pq.reset_now();

    // Enqueue events out of order.
    pq.set_timeout(Event::Test2, 10);
    pq.set_timeout(Event::EndOfCallback, 10);
    pq.set_timeout(Event::Test1, 10);

    // Assert they're dequeued in increasing order.
    {
        auto event = pq.next_event();
        CHECK(event.event_id == Event::EndOfCallback);
        CHECK(event.cyc_elapsed == 10);
    }
    {
        auto event = pq.next_event();
        CHECK(event.event_id == Event::Test1);
        CHECK(event.cyc_elapsed == 0);
    }
    {
        auto event = pq.next_event();
        CHECK(event.event_id == Event::Test2);
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
    using PQ = audio::EventQueue<EventClass>;
    PQ pq;

    pq.reset_now();
    pq.set_timeout(EventClass::EndOfCallback, 10);
    {
        auto event = pq.next_event();
        CHECK(event.event_id == EventClass::EndOfCallback);
        CHECK(event.cyc_elapsed == 10);
    }
}
