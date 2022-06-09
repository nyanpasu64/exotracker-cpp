#define sequencer_INTERNAL public
#include "sequencer.h"
#include "spc700_math.h"
#include "doc_util/event_search.h"
#include "util/expr.h"
#include "util/format.h"
#include "util/release_assert.h"
#include "util/math.h"

#include <fmt/core.h>

#include <algorithm>  // std::min
#include <limits>  // std::numeric_limits
#include <string>
#include <type_traits>  // std::is_signed_v

//#define SEQUENCER_DEBUG

namespace audio::synth::sequencer {

/// Why signed? See TickT definition for details.
static_assert(std::is_signed_v<TickT>, "TickT must be signed");

using namespace doc;
using doc_util::track_util::song_length;
using doc_util::event_search::EventSearch;

static TickT time_in_pattern(TickT now, PatternRef pattern) {
    return now - pattern.begin_tick;
}

EventIterator EventIterator::at_time(SequenceTrackRef track, TickT now) {
    auto patterns = TrackPatternIter::at_time(track, now).iter;
    auto maybe_pattern = patterns.peek(track);
    if (maybe_pattern) {
        PatternRef pattern = *maybe_pattern;

        auto ev = EventSearch(pattern.events);
        auto ev_index =
            ev.tick_begin(time_in_pattern(now, pattern)) - pattern.events.begin();

        // Return EventIterator with in-bound patterns, and possibly OOB event idx.
        return EventIterator {
            ._patterns = patterns,
            ._event_idx = (EventIndex) ev_index,
        };
    }

    // Return EventIterator with patterns past the end.
    return EventIterator {
        ._patterns = patterns,
        ._event_idx = 0,
    };
}

static void ev_next_pattern(EventIterator & self, SequenceTrackRef track) {
    self._event_idx = 0;
    self._patterns.next(track);
}

struct EventRef {
    // Only used in SEQUENCER_DEBUG.
    PatternRef pattern;

    TickT event_time;
    /// If EventRef points to the end of a pattern, event_or_end is nullptr.
    RowEvent const* event_or_end;
};

using MaybeEventRef = std::optional<EventRef>;

static MaybeEventRef ev_get_curr_event(
    EventIterator const& self, SequenceTrackRef track, EffColIndex n_effect_col
) {
    MaybePatternRef p = self._patterns.peek(track);
    if (p) {
        TimedEventsRef events = p->events;
        release_assert(self._event_idx <= events.size());
        if (self._event_idx >= events.size()) {
            return EventRef {
                .pattern = *p,
                .event_time = p->end_tick,
                .event_or_end = nullptr,
            };
        } else {
            return EventRef {
                .pattern = *p,
                .event_time =
                    p->begin_tick + events[self._event_idx].time(n_effect_col),
                .event_or_end = &events[self._event_idx].v,
            };
        }
    } else {
        return {};
    }
}

static void ev_next_event(EventIterator & self) {
    self._event_idx++;
}

ChannelSequencer::ChannelSequencer() {
    /*
    On ticks without events, ChannelSequencer should return a 0-length vector.
    On ticks with events, ChannelSequencer should return a 1-length vector.

    The only time we should return more than 1 event is with broken documents,
    where multiple events occur at the same time
    (usually due to early events being offset later,
    or later events being offset earlier).

    Later events prevent earlier events from being offset later;
    instead they will pile up at the same time as the later event.

    We should never reach or exceed 6 events simultaneously (including note cuts
    injected at pattern end).
    */
    _events_this_tick.reserve(6);
    stop_playback();
}

void ChannelSequencer::stop_playback() {
    // Reset fields.
    // Unnecessary?
    _events_this_tick.clear();

    _now = 0;

    _ignore_ordering_errors = false;

    _curr_pattern_next_ev = {};
}

// # ChannelSequencer::next_tick() helpers.

static void print_chip_channel(ChannelSequencer const& self) {
    fmt::print(stderr, "seq {},{} ", self._chip_index, self._chan_index);
}


std::tuple<SequencerTime, EventsRef> ChannelSequencer::next_tick(
    doc::Document const & document
) {
    _events_this_tick.clear();

    // Assert that seek() was called earlier.
    release_assert(_curr_pattern_next_ev);
    EventIterator & events = *_curr_pattern_next_ev;

    // Document-level operations, not bound to current track/pattern.
    doc::SequencerOptions const& options = document.sequencer_options;
    const TickT note_gap_ticks = options.note_gap_ticks;

    {
        auto const nchip = document.chips.size();
        release_assert(_chip_index < nchip);
        auto const nchan = document.chip_index_to_nchan(_chip_index);
        release_assert(_chan_index < nchan);
    }

    doc::SequenceTrackRef track = document.sequence[_chip_index][_chan_index];
    const auto n_effect_col = track.settings.n_effect_col;

    #ifdef SEQUENCER_DEBUG
    print_chip_channel(*this);
    fmt::print(stderr,
        "begin tick {}\n", _now
    );
    if (auto pattern = events._patterns.peek(track)) {
        fmt::print(stderr,
            "\tcurrent event: block {}, pattern begin {}, index {}\n",
            pattern->block,
            pattern->begin_tick,
            events._event_idx);
    } else {
        fmt::print(stderr,
            "\tpast the last pattern\n"
        );
    }
    #endif

    // SequencerTime (playback point shown to GUI) is current tick (just occurred), not
    // next tick. This is a subjective choice?
    const auto seq_chan_time = SequencerTime(_now);

    // The real amkd checks if the note timer is 0, if so fetching events and advancing
    // patterns until reaching the next note/rest/tie, and if not checks for early
    // note cut.
    //
    // Unlike amkd, we call ev_get_curr_event() in the outer loop. Not sure why.
    bool events_processed = false;

    // This is a hack to adapt amkd's algorithm to exo's data model (which supports
    // gaps between patterns, which we trigger note-offs on).
    bool pattern_ended = false;

    while (MaybeEventRef next_ev = ev_get_curr_event(events, track, n_effect_col)) {
        TickT time_to_next_ev = next_ev->event_time - _now;

        #ifdef SEQUENCER_DEBUG
        fmt::print(stderr,
            "\tinspecting event: block {} (pattern begin {}) .events[{}] time {} ({} ticks left)\n",
            next_ev->pattern.block,
            next_ev->pattern.begin_tick,
            events._event_idx,
            next_ev->event_time,
            time_to_next_ev);
        #endif

        if (!_ignore_ordering_errors) {
            // This *may* be negative on malformed documents?
            if (time_to_next_ev < 0) {
                print_chip_channel(*this);
                fmt::print(stderr,
                    "invalid document: block {} (pattern begin {}) "
                    ".events[{}] time {} is in the past (now {})!\n",
                    next_ev->pattern.block, next_ev->pattern.begin_tick,
                    events._event_idx, next_ev->event_time, _now);
            }
            assert(time_to_next_ev >= 0);
        }

        // The actual amkd has a counter for the remaining duration of the current
        // event.
        bool curr_ev_ended = time_to_next_ev == 0;

        if (curr_ev_ended) {
            #ifdef SEQUENCER_DEBUG
            fmt::print(stderr, "\t\treached\n");
            #endif

            // I think this should be set for both pattern ends and regular events?
            // amkd treats "remaining note length = 0" identically (skipping early note
            // off), regardless if a note or pattern end follows.
            events_processed = true;

            if (next_ev->event_or_end == nullptr) {
                // Process "end of pattern" virtual event if reached.

                #ifdef SEQUENCER_DEBUG
                fmt::print(stderr, "\t\tnext pattern\n");
                #endif

                if (pattern_ended) {
                    print_chip_channel(*this);
                    fmt::print(stderr,
                        "invalid document: two pattern changes at same tick {}!\n",
                        _now);
                }

                // Prevent an infinite loop from happening in case of code bug.
                assert(!pattern_ended);
                if (pattern_ended) {
                    // This is reachable if the document is invalid and has two pattern
                    // ends on the same tick. Fail gracefully instead of crashing.
                    //
                    // (The real amkd can process an unlimited number of zero-length
                    // patterns on a single tick. This is not valid in exotracker.)
                    break;
                }
                pattern_ended = true;

                // May take us past the last pattern in the song. If so, do nothing and
                // loop once we reach song_length().
                ev_next_pattern(events, track);
                continue;

            } else {
                // Process TimedRowEvent if reached.
                assert(next_ev->event_or_end);

                #ifdef SEQUENCER_DEBUG
                fmt::print(stderr,
                    "\t\tplaying event time {}\n",
                    next_ev->event_time);
                #endif

                _events_this_tick.push_back(*next_ev->event_or_end);
                ev_next_event(events);
                continue;
            }

        } else {
            #ifdef SEQUENCER_DEBUG
            fmt::print(stderr,
                "\t\tevent rejected\n",
                time_to_next_ev);
            #endif

            // No more events this tick.
            if (events_processed) {
                // TODO if events ended this tick, save qxy timer based on
                // time_to_next_ev

            } else {
                #ifdef SEQUENCER_DEBUG
                fmt::print(stderr,
                    "\tno events processed, {} ticks until next event\n",
                    time_to_next_ev);
                #endif

                // If no events ended this tick, and next event is a loop end or note,
                // check for early note cuts. If it's a tie, don't.
                // TODO skip if legato
                if (!next_ev->event_or_end || next_ev->event_or_end->note) {
                    // TODO decrement qxy timer, if 0 cut note
                    if (time_to_next_ev == note_gap_ticks) {
                        #ifdef SEQUENCER_DEBUG
                        fmt::print(stderr, "\t\tinjecting early note cut\n");
                        #endif
                        _events_this_tick.push_back(RowEvent{.note = {NOTE_CUT}});
                    }
                }
            }
            break;
        }
    }

    // Next tick (not processed yet). We must increment _now *after* processing notes,
    // otherwise we'd fail to process notes at tick 0 (or seek(_)).
    _now++;
    if (_now >= song_length(document.sequence)) {
        #ifdef SEQUENCER_DEBUG
        fmt::print(stderr,
            "\t{} >= length {}, looping song to time 0\n",
            _now,
            song_length(document.sequence));
        #endif

        // TODO add user-specified loop point alongside bookmarks and tsig changes
        // This sets _ignore_ordering_errors=true. We later set it to false. Shrug.
        seek(document, 0);
        pattern_ended = true;
    }

    // If a pattern ends this tick, if there are no note events this tick, cut the
    // running note (if any). (We can't insert a note cut alongside note events, since
    // note cuts override note-ons.)
    //
    // This logic is not found in amkd (which doesn't have gaps between patterns).
    if (pattern_ended) {
        // TODO skip if legato
        bool note_this_tick = false;
        for (RowEvent const& ev : _events_this_tick) {
            if (ev.note) {
                note_this_tick = true;
                break;
            }
        }
        if (!note_this_tick) {
            #ifdef SEQUENCER_DEBUG
            fmt::print(stderr,
                "\tpattern ended, no new events, injecting pattern-end note cut\n"
            );
            #endif
            _events_this_tick.insert(
                _events_this_tick.begin(), RowEvent{.note = {NOTE_CUT}}
            );
        } else {
            #ifdef SEQUENCER_DEBUG
            fmt::print(stderr,
                "\tpattern ended, new events\n"
            );
            #endif
        }
    }

    _ignore_ordering_errors = false;
    return {seq_chan_time, _events_this_tick};
}


void ChannelSequencer::seek(doc::Document const & document, TickT time) {
    #ifdef SEQUENCER_DEBUG
    print_chip_channel(*this);
    fmt::print(stderr, "seek {}\n", time);
    #endif
    // Should initialize the same fields as stop_playback().

    {
        auto const nchip = document.chips.size();
        release_assert(_chip_index < nchip);
        auto const nchan = document.chip_index_to_nchan(_chip_index);
        release_assert(_chan_index < nchan);
    }

    // Set real time.
    _now = time;

    // Set next event to play.
    doc::SequenceTrackRef track = document.sequence[_chip_index][_chan_index];
    _curr_pattern_next_ev = EventIterator::at_time(track, time);

    // Users may use early notes at the beginning or middle of patterns.
    // If the user initiates playback at a time with early notes,
    // just play them immediately instead of complaining about past notes,
    // since the document isn't invalid.
    _ignore_ordering_errors = true;
}

static bool is_playing(ChannelSequencer const& self) {
    return self._curr_pattern_next_ev.has_value();
}

void ChannelSequencer::doc_edited(doc::Document const & document) {
    #ifdef SEQUENCER_DEBUG
    print_chip_channel(*this);
    fmt::print(stderr, "doc_edited\n");
    #endif

    if (!is_playing(*this)) {
        return;
    }

    {
        auto const nchip = document.chips.size();
        release_assert(_chip_index < nchip);
        auto const nchan = document.chip_index_to_nchan(_chip_index);
        release_assert(_chan_index < nchan);
    }

    // Set next event to play.
    doc::SequenceTrackRef track = document.sequence[_chip_index][_chan_index];
    const auto n_effect_col = track.settings.n_effect_col;

    _curr_pattern_next_ev = EventIterator::at_time(track, _now);

    // Advance past events anchored after now but delayed before now.
    EventIterator & events = *_curr_pattern_next_ev;
    while (true) {
        MaybeEventRef ev =
            ev_get_curr_event(events, track, n_effect_col);
        if (ev && ev->event_time < _now) {
            ev_next_event(events);
        } else {
            break;
        }
    }

    // Can reducing document length create ordering errors in
    // `ChannelSequencer::next_tick()`: `now > (next event = pattern end)`?
    //
    // No. If the song gets shorter, when this calls
    // `_curr_pattern_next_ev = EventIterator::at_time(track, _now)`,
    // `EventIterator::_patterns` is OOB instead of pointing to a pattern we passed,
    // so `ev_get_curr_event()` returns nullopt.
    _ignore_ordering_errors = false;
}

// end namespaces
}
