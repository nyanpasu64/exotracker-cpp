#pragma once

#include "doc/events.h"
#include "doc/timed_events.h"
#include "doc/event_list.h"

namespace doc {
    using namespace doc::events;
    using namespace doc::timed_events;
    using namespace doc::event_list;
}

namespace edit_util::shorthand {

using doc::TimeInPattern;
using doc::BeatFraction;
using doc::TickT;
using doc::Note;

static TimeInPattern at(BeatFraction anchor_beat) {
    return TimeInPattern{anchor_beat, 0};
}

static TimeInPattern at(int start, int num, int den) {
    return TimeInPattern{start + BeatFraction(num, den), 0};
}

static TimeInPattern at_delay(BeatFraction anchor_beat, TickT tick_offset) {
    return TimeInPattern{anchor_beat, tick_offset};
}

static TimeInPattern at_delay(int start, int num, int den, TickT tick_offset) {
    return TimeInPattern{start + BeatFraction(num, den), tick_offset};
}

static Note pitch(int octave, int semitone) {
    return Note{static_cast<doc::ChromaticInt>(12 * octave + semitone)};
}

}
