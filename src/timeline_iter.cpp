#include "timeline_iter.h"
#include "util/enumerate.h"
#include "util/format.h"
#include "util/release_assert.h"

#include <stdexcept>

//#define TIME_CONV_DEBUG

#ifdef TIME_CONV_DEBUG
    #include <fmt/core.h>
    #define DEBUG_PRINT(...)  fmt::print(stderr, __VA_ARGS__)
#else
    #define DEBUG_PRINT(...)
#endif

namespace timeline_iter {

using CellIter = doc::TimelineCellIterRef;

static
std::tuple<doc::MaybePatternRef, CellIter> pattern_iter_seek(
    doc::TimelineCellRef cell_ref, BeatFraction beat
) {
    auto iter = CellIter(cell_ref);

    // The cursor remains at a fixed point.
    // Each block occurs later than the previous block.
    // Search for first block ending after the cursor, or OOB if none exists.
    while (auto maybe_pattern = iter.next()) {
        doc::PatternRef pattern = *maybe_pattern;
        if (beat < pattern.end_time) {
            return {pattern, iter};
        }
    }
    return {{}, iter};
}

doc::PatternRef pattern_or_end(doc::TimelineCellRef cell_ref, BeatFraction beat) {
    return std::get<0>(pattern_iter_seek(cell_ref, beat))
        .value_or(doc::PatternRef{cell_ref.cell.size()});
}

#if 0
GridAndBeat real_time(
    doc::Document const& document,
    ChipIndex chip,
    ChannelIndex channel,
    GridBlockBeat rel_time
) {
    // TODO test this
    auto & block = document.chip_channel_timelines
        [chip][channel][rel_time.grid]._raw_blocks[rel_time.block];

    return GridAndBeat{rel_time.grid, block.begin_time + rel_time.beat};
}
#endif


// It might be useful for BlockIterator prev/next to return a different first value
// if now is between blocks. But that'll be done later.
template<>
ForwardBlockIterator ForwardBlockIterator::from_beat(
    doc::TimelineChannelRef timeline, GridAndBeat now
) {
    auto [pat, iter] = pattern_iter_seek(timeline[now.grid], now.beat);

    std::vector<doc::PatternRef> cell_patterns;

    if (pat) {
        cell_patterns.push_back(*pat);
    }
    while (auto pat = iter.next()) {
        cell_patterns.push_back(*pat);
    }

    return BlockIterator {
        ._timeline = timeline,

        ._orig_grid = now.grid,
        ._orig_pattern_start = (pat ? pat->begin_time : timeline[now.grid].nbeats),

        ._grid = now.grid,
        ._cell_patterns = std::move(cell_patterns),
        ._pattern = 0,
    };
}

template<>
ReverseBlockIterator ReverseBlockIterator::from_beat(
    doc::TimelineChannelRef timeline, GridAndBeat now
) {
    auto iter = CellIter(timeline[now.grid]);

    std::vector<doc::PatternRef> cell_patterns;

    // Include all patterns, except those starting after now.
    while (auto pat = iter.next()) {
        if (pat->begin_time > now.beat) {
            break;
        }
        cell_patterns.push_back(*pat);
    }

    return BlockIterator {
        ._timeline = timeline,

        ._orig_grid = now.grid,
        ._orig_pattern_start = (cell_patterns.size() ? cell_patterns.back().begin_time : 0),

        ._grid = now.grid,
        ._cell_patterns = cell_patterns,
        ._pattern = 0,
    };
}

/// Would it work better as an actual state machine? Maybe, I don't know.
/// I don't know if it's better to separate out the "first call, invalid block"
/// and "subsequent call" logic.
template<>
std::optional<BlockIteratorRef> ForwardBlockIterator::next() {
    scrBegin;

    release_assert(_grid < _timeline.size());
    goto begin;

    for (; _wrap_count <= 1; _wrap_count++) {
        for (_grid = 0; _grid < _timeline.size(); _grid++) {
            {
                _cell_patterns.clear();

                auto cell_iter = CellIter(_timeline[_grid]);
                while (auto p = cell_iter.next()) {
                    _cell_patterns.push_back(*p);
                }
            }

            begin:
            DEBUG_PRINT("forward, patterns size {}\n", _cell_patterns.size());
            for (_pattern = 0; _pattern < _cell_patterns.size(); _pattern++) {
                scrBeginScope;
                doc::PatternRef pattern = _cell_patterns[_pattern];
                if (
                    std::tuple(_wrap_count, _grid, pattern.begin_time)
                    > std::tuple(1, _orig_grid, _orig_pattern_start)
                ) {
                    goto end;
                }

                DEBUG_PRINT(
                    "forward, grid {}, time {} to {}\n",
                    _grid,
                    PATTERN.begin_time,
                    format_frac(PATTERN.end_time)
                );
                scrReturnEndScope((
                    BlockIteratorRef{(Wrap) _wrap_count, _grid, pattern}
                ));
            }
        }
    }  // fallthrough to end
    end:
    while (true) {
        DEBUG_PRINT("forward, nullopt\n");
        scrReturn(std::nullopt);
    }

    scrFinishUnreachable;
    throw std::logic_error("Reached end of ForwardBlockIterator::next()");
}

template<>
std::optional<BlockIteratorRef> ReverseBlockIterator::next() {
    scrBegin;

    release_assert(_grid < _timeline.size());
    goto begin;

    for (; _wrap_count >= -1; _wrap_count--) {
        if (_timeline.size())
        for (_grid = _timeline.size() - 1; ; ) {
            {
                _cell_patterns.clear();

                auto cell_iter = CellIter(_timeline[_grid]);
                while (auto p = cell_iter.next()) {
                    _cell_patterns.push_back(*p);
                }
            }

            begin:
            DEBUG_PRINT("reverse, patterns size {}\n", _cell_patterns.size());
            if (_cell_patterns.size())
            for (_pattern = _cell_patterns.size() - 1; ;) {
                scrBeginScope;
                doc::PatternRef pattern = _cell_patterns[_pattern];
                if (
                    std::tuple(_wrap_count, _grid, pattern.begin_time)
                    < std::tuple(-1, _orig_grid, _orig_pattern_start)
                ) {
                    goto end;
                }

                DEBUG_PRINT(
                    "reverse, grid {}, time {} to {}\n",
                    _grid,
                    PATTERN.begin_time,
                    format_frac(PATTERN.end_time)
                );
                scrReturnEndScope((
                    BlockIteratorRef{(Wrap) _wrap_count, _grid, pattern}
                ));

                if (_pattern == 0) break;
                _pattern--;
            }

            if (_grid == GridIndex(0)) break;
            _grid--;
        }
    }  // fallthrough to end
    end:
    while (true) {
        DEBUG_PRINT("reverse, nullopt\n");
        scrReturn(std::nullopt);
    }

    scrFinishUnreachable;
    throw std::logic_error("Reached end of ReverseBlockIterator::next()");
}

}
