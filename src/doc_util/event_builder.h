#pragma once

#include "doc/events.h"
#include "doc/timed_events.h"
#include "doc/event_list.h"
#include "doc/effect_names.h"
#include "util/release_assert.h"

#include <cstdint>

namespace doc_util::event_builder {

using namespace doc::events;
using namespace doc::timed_events;
using namespace doc::event_list;

namespace effs = doc::effect_names;

/// C++, can we have Rust traits?
/// C++: We have Rust traits at home
/// Rust traits at home:
class EventBuilder {
// types
    using Self = EventBuilder;

// fields
    TimedRowEvent _ev;
    EffColIndex _n_occupied_effect = 0;

// impl
public:
    EventBuilder(BeatFraction anchor_beat, std::optional<Note> note)
        : _ev{anchor_beat, RowEvent{note}}
    {}

    operator TimedRowEvent() {
        return _ev;
    }

    Self & instr(InstrumentIndex i) {
        _ev.v.instr = i;
        return *this;
    }

    Self & volume(Volume v) {
        _ev.v.volume = v;
        return *this;
    }

    Self & delay(TickT tick_offset) {
        release_assert(_n_occupied_effect < MAX_EFFECTS_PER_EVENT);
        auto v = (tick_offset < 0) ? (-tick_offset + 0x80) : tick_offset;

        _ev.v.effects[_n_occupied_effect] = Effect(effs::DELAY, EffectValue(v));
        _n_occupied_effect++;
        return *this;
    }

    template<typename T>
    Self & effect(T name, EffectValue value) {
        release_assert(_n_occupied_effect < MAX_EFFECTS_PER_EVENT);

        _ev.v.effects[_n_occupied_effect] = Effect(name, value);
        _n_occupied_effect++;
        return *this;
    }

    Self & no_effect() {
        _n_occupied_effect++;
        return *this;
    }
};

static BeatFraction at(int start, int num, int den) {
    return start + BeatFraction(num, den);
}

static Note pitch(int octave, int semitone) {
    return Note(Chromatic(
        std::clamp(12 * octave + semitone, 0, CHROMATIC_COUNT - 1)
    ));
}

}
