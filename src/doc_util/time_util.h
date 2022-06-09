#pragma once

#include "doc.h"

#include <cstdint>
#include <optional>

namespace doc_util::time_util {

using namespace doc;

// Global beat iteration
// I'm sincerely sorry for the complexity of these APIs.

struct Row {
    TickT time;

    // Never accessed except in is_beat()! (draw_pattern_background() only draws beat
    // numbers, not row numbers.)
    std::optional<int16_t> maybe_beat_in_measure;

    bool is_beat() const {
        return maybe_beat_in_measure.has_value();
    }

    // Never called!
    bool is_measure() const {
        return maybe_beat_in_measure == 0;
    }
};

struct Beat {
    TickT time;
    int16_t beat_in_measure;

    bool is_measure() const {
        return beat_in_measure == 0;
    }
};

/// Return the index of the nearest measure <= now (or the measure count <= now
/// minus 1).
int measure_at(Document const& doc, TickT now);

struct BeatIterResult;

class BeatIter {
    /// Non-null.
    Document const* _doc;
    /// Invariant: always beat-aligned.
    TickT _curr_time;

    TickT _curr_ticks_per_beat;
    // TODO add support for mid-song time signature changes/beat resets
    int _beat_in_measure;
    int _curr_beats_per_measure;

// impl
    BeatIter(
        Document const& doc,
        TickT curr_time,
        TickT curr_ticks_per_beat,
        int beat_in_measure,
        int curr_beats_per_measure);

public:
    /// Return an iterator pointing to the nearest beat <= now. Note that this is
    /// inconsistent with TrackPatternIter!
    static BeatIterResult at_time(Document const& doc, TickT now);

    [[nodiscard]] Beat peek() const;
    TickT ticks_until_next_beat() const;

    /// Advances to the next beat.
    void next();

    /// Reverses to the previous beat. If current time is 0, does nothing.
    bool try_prev();
};

struct BeatIterResult {
    BeatIter iter;
    /// True if iter was rounded to an earlier time than supplied.
    bool snapped_earlier;
};

struct RowIterResult;

class RowIter {
    BeatIter _beat_iter;
    /// Do not change while object exists.
    TickT _ticks_per_row;
    int _row_in_beat;

// impl
    RowIter(BeatIter beat_iter, TickT ticks_per_row, int row_in_beat);

public:
    /// Return an iterator pointing to the nearest row <= now. Note that this is
    /// inconsistent with TrackPatternIter!
    static RowIterResult at_time(Document const& doc, TickT now, TickT ticks_per_row);

private:
    /// peek() relative to _beat_iter.peek().
    TickT time_rel_after_beat() const;

public:
    [[nodiscard]] Row peek() const;

    /// Advances to the next row.
    void next();

    /// Reverses to the previous row. If current time is 0, does nothing.
    bool try_prev();
};

struct RowIterResult {
    RowIter iter;
    /// True if iter was rounded to an earlier time than supplied.
    bool snapped_earlier;
};

struct MeasureIterResult;

class MeasureIter {
    BeatIter _beat_iter;

// impl
    MeasureIter(BeatIter beat_iter);

public:
    /// Return an iterator pointing to the nearest row <= now. Note that this is
    /// inconsistent with TrackPatternIter!
    static MeasureIterResult at_time(Document const& doc, TickT now);

    [[nodiscard]] TickT peek() const;

    /// Advances to the next measure.
    void next();

    /// Reverses to the previous measure. If current time is 0, does nothing.
    void try_prev();
};

struct MeasureIterResult {
    MeasureIter iter;
    /// True if iter was rounded to an earlier time than supplied.
    bool snapped_earlier;
};

} // namespaces

