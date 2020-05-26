#ifdef UNITTEST

#include "envelope.h"
#include "doc/instr.h"

#include <doctest.h>

namespace audio::synth::envelope {

namespace instr = doc::instr;
using ByteIterator = EnvelopeIterator<instr::ByteEnvelope>;

TEST_CASE("Test EnvelopeIterator on an empty envelope") {
    instr::ByteEnvelope const & env = ByteIterator::_empty_env;

    ByteIterator iter(&instr::Instrument::volume, 11);
    CHECK(iter.next_raw(env) == 0);
    CHECK(iter.next_raw(env) == 0);

    iter.note_on({});
    CHECK(iter.next_raw(env) == 11);
    CHECK(iter.next_raw(env) == 11);
    CHECK(iter.next_raw(env) == 11);

    iter.note_cut();
    CHECK(iter.next_raw(env) == 0);
    CHECK(iter.next_raw(env) == 0);
}

TEST_CASE("Test EnvelopeIterator on a non-empty instrument") {
    instr::Instrument i;
    i.volume.values = {15, 10, 5};

    // silence
    ByteIterator iter(&instr::Instrument::volume, 11);
    CHECK(iter.next_raw(i.volume) == 0);
    CHECK(iter.next_raw(i.volume) == 0);

    // play a note. should trigger.
    iter.note_on({1});
    CHECK(iter.next_raw(i.volume) == 15);
    CHECK(iter.next_raw(i.volume) == 10);
    CHECK(iter.next_raw(i.volume) == 5);
    CHECK(iter.next_raw(i.volume) == 5);

    // play a note with no instrument. should trigger.
    iter.note_on({});
    CHECK(iter.next_raw(i.volume) == 15);
    CHECK(iter.next_raw(i.volume) == 10);
    CHECK(iter.next_raw(i.volume) == 5);
    CHECK(iter.next_raw(i.volume) == 5);

    // switch to the same instrument. should not trigger.
    iter.switch_instrument(1);
    CHECK(iter.next_raw(i.volume) == 5);

    // switch to a different instrument. should trigger.
    iter.switch_instrument(2);
    CHECK(iter.next_raw(i.volume) == 15);

    // note cut.
    iter.note_cut();
    CHECK(iter.next_raw(i.volume) == 0);
    CHECK(iter.next_raw(i.volume) == 0);
}

TEST_CASE("Test EnvelopeIterator on a real document") {
    instr::Instrument instrument;
    instrument.volume.values = {15, 10, 5};

    // Instruments 0 and 1 are populated.
    doc::Document document{doc::DocumentCopy{}};
    document.instruments[0] = instrument;
    document.instruments[1] = instrument;


    // silence
    ByteIterator iter(&instr::Instrument::volume, 11);
    CHECK(iter.next(document) == 0);
    CHECK(iter.next(document) == 0);

    // play a note with no instrument.
    iter.note_on({});
    CHECK(iter.next(document) == 11);
    CHECK(iter.next(document) == 11);
    iter.note_cut();
    CHECK(iter.next(document) == 0);

    // play a note with an empty instrument.
    iter.note_on({2});
    CHECK(iter.next(document) == 11);
    CHECK(iter.next(document) == 11);
    iter.note_cut();
    CHECK(iter.next(document) == 0);

    // play a note. should trigger.
    iter.note_on({1});
    CHECK(iter.next(document) == 15);
    CHECK(iter.next(document) == 10);
    CHECK(iter.next(document) == 5);
    CHECK(iter.next(document) == 5);

    // play a note with no instrument.
    iter.note_on({});
    CHECK(iter.next(document) == 15);
    CHECK(iter.next(document) == 10);
    CHECK(iter.next(document) == 5);
    CHECK(iter.next(document) == 5);

    // switch to the same instrument. should not trigger.
    iter.switch_instrument(1);
    CHECK(iter.next(document) == 5);

    // delete instrument 1. hold previous value (behavior doesn't matter).
    document.instruments[1] = {};
    CHECK(iter.next(document) == 5);

    // switch to a populated instrument. should trigger.
    iter.switch_instrument(0);
    CHECK(iter.next(document) == 15);
    CHECK(iter.next(document) == 10);
    CHECK(iter.next(document) == 5);
    CHECK(iter.next(document) == 5);

    // switch to an unpopulated instrument. reset to default value (behavior doesn't matter).
    iter.switch_instrument(2);
    CHECK(iter.next(document) == 11);
    CHECK(iter.next(document) == 11);
    iter.note_cut();
    CHECK(iter.next(document) == 0);
}

}  // namespace

#endif
