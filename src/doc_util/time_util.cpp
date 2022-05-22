#include "time_util.h"
#include "util/release_assert.h"

namespace doc_util::time_util {

// Global beat iteration

BeatIter::BeatIter(
    Document const& doc,
    TickT curr_time,
    TickT curr_ticks_per_beat,
    int beat_in_measure,
    int curr_beats_per_measure)
:
    _doc(&doc)
    , _curr_time(curr_time)
    , _curr_ticks_per_beat(curr_ticks_per_beat)
    , _beat_in_measure(beat_in_measure)
    , _curr_beats_per_measure(curr_beats_per_measure)
{}

// Keep in sync with BeatIter measure counting!
int measure_at(Document const& doc, TickT now) {
    // TODO add support for time signature changes
    SequencerOptions const& opt = doc.sequencer_options;
    TickT ticks_per_beat = opt.ticks_per_beat;

    /*
    Return the index of the nearest measure <= now (or the count of measures <= now
    minus 1).

    This is accomplished by (for each tempo change <= the cursor) counting measure
    begins within [prev change .. curr change) using ceildiv, and (for the cursor)
    returning the current measure index (or the measure count within
    [final change .. cursor] minus 1) using floordiv.

    Counting is hard.
    */
    const TickT LAST_TIME_SIG_CHANGE = 0;
    const int beats_per_measure = doc.sequencer_options.beats_per_measure;
    return (now - LAST_TIME_SIG_CHANGE) / (ticks_per_beat * beats_per_measure);
}

BeatIterResult BeatIter::at_time(Document const& doc, TickT now) {
    release_assert(now >= 0);

    SequencerOptions const& opt = doc.sequencer_options;
    TickT ticks_per_beat = opt.ticks_per_beat;
    const int beats_per_measure = opt.beats_per_measure;

    // Find nearest beat <= now.
    // TODO support mid-song changes to ticks_per_beat, and measure resets
    auto beat_index = now / ticks_per_beat;
    auto curr_beat_time = beat_index * ticks_per_beat;

    int beat_in_measure = beat_index % beats_per_measure;

    bool snapped_earlier = curr_beat_time != now;

    return BeatIterResult {
        .iter = BeatIter(
            doc, curr_beat_time, ticks_per_beat, beat_in_measure, beats_per_measure
        ),
        .snapped_earlier = snapped_earlier,
    };
}

Beat BeatIter::peek() const {
    return Beat {
        .time = _curr_time,
        // All beats are beats, but only "first in measure" beats are measures.
        .beat_in_measure = (int16_t) _beat_in_measure,
    };
}

TickT BeatIter::ticks_until_next_beat() const {
    // TODO flesh out after adding mid-beat measure resets
    return _curr_ticks_per_beat;
}

void BeatIter::next() {
    _curr_time += _curr_ticks_per_beat;
    _beat_in_measure = (_beat_in_measure + 1) % _curr_beats_per_measure;

    // TODO change when adding time signature changes
    assert(_curr_time % _curr_ticks_per_beat == 0);
}

bool BeatIter::try_prev() {
//    assert(_curr_time - _curr_ticks_per_beat >= 0);
    bool moved_back;

    if (_curr_time - _curr_ticks_per_beat < 0) {
        // The only way that searching for a previous beat returns a negative value,
        // should be if now is 0.
        assert(_curr_time == 0);

        moved_back = false;
        _curr_time = 0;
        _beat_in_measure = 0;
    } else {
        moved_back = true;
        _curr_time -= _curr_ticks_per_beat;
        _beat_in_measure =
            (_beat_in_measure + _curr_beats_per_measure - 1) % _curr_beats_per_measure;
    }
    assert(_curr_time % _curr_ticks_per_beat == 0);
    if (_curr_time == 0) {
        assert(_beat_in_measure == 0);
    }
    return moved_back;
}

RowIter::RowIter(BeatIter beat_iter, TickT ticks_per_row, int row_in_beat)
    : _beat_iter(beat_iter)
    , _ticks_per_row(ticks_per_row)
    , _row_in_beat(row_in_beat)
{}

RowIterResult RowIter::at_time(Document const& doc, TickT now, TickT ticks_per_row) {
    auto iter = BeatIter::at_time(doc, now).iter;
    auto beat_tick = iter.peek().time;

    // Round down to the nearest row <= now.
    int row_in_beat = (now - beat_tick) / ticks_per_row;

    // snapped_earlier=true is a subset of BeatIterResult::snapped_earlier=true.
    bool snapped_earlier = (row_in_beat * ticks_per_row) + beat_tick != now;
    return RowIterResult {
        .iter = RowIter(iter, ticks_per_row, row_in_beat),
        .snapped_earlier = snapped_earlier,
    };
}

TickT RowIter::time_rel_after_beat() const {
    return _row_in_beat * _ticks_per_row;
}

Row RowIter::peek() const {
    Beat beat = _beat_iter.peek();

    // TODO add asserts
    TickT row_tick = beat.time + time_rel_after_beat();
    if (row_tick != beat.time) {
        return Row {
            .time = row_tick,
            .maybe_beat_in_measure = {},
        };
    } else {
        return Row {
            .time = beat.time,
            .maybe_beat_in_measure = beat.beat_in_measure,
        };
    }
}

void RowIter::next() {
    _row_in_beat++;
    if (time_rel_after_beat() >= _beat_iter.ticks_until_next_beat()) {
        _row_in_beat = 0;
        _beat_iter.next();
    }
}

bool RowIter::try_prev() {
    if (_row_in_beat != 0) {
        _row_in_beat--;
        return true;
    } else {
        if (!_beat_iter.try_prev()) return false;
        auto beat_end_tick = _beat_iter.ticks_until_next_beat();

        // Find the last row < beat_end_tick.
        _row_in_beat = (beat_end_tick - 1) / _ticks_per_row;
        return true;
    }
}

MeasureIter::MeasureIter(BeatIter beat_iter)
    : _beat_iter(beat_iter)
{}

MeasureIterResult MeasureIter::at_time(Document const& doc, TickT now) {
    auto [beat_iter, beat_snapped_earlier] = BeatIter::at_time(doc, now);
    bool snapped_earlier = beat_snapped_earlier;

    Beat beat;
    while (beat = beat_iter.peek(), !beat.is_measure()) {
        release_assert(beat.time > 0);
        beat_iter.try_prev();
        snapped_earlier = true;
    }
    return MeasureIterResult {
        .iter = MeasureIter(beat_iter),
        .snapped_earlier = snapped_earlier,
    };
}

TickT MeasureIter::peek() const {
    auto beat = _beat_iter.peek();
    assert(beat.is_measure());
    return beat.time;
}

void MeasureIter::next() {
    _beat_iter.next();
    Beat beat;
    while (beat = _beat_iter.peek(), !beat.is_measure()) {
        _beat_iter.next();
    }
}

void MeasureIter::try_prev() {
    _beat_iter.try_prev();
    Beat beat;
    while (beat = _beat_iter.peek(), !beat.is_measure()) {
        release_assert(beat.time > 0);
        _beat_iter.try_prev();
    }
}

} // namespaces
