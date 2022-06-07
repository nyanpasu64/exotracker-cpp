#include "gui_time.h"
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

namespace gui::gui_time {

using FrameIter = doc_util::track_util::FramePatternIterRef;


// It might be useful for GuiPatternIter prev/next to return a different first value
// if now is between blocks. But that'll be done later.
template<>
FwdGuiPatternIter FwdGuiPatternIter::from_beat(
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

    return FwdGuiPatternIter {
        ._timeline = timeline,

        ._grid = now.grid,
        ._cell_patterns = std::move(cell_patterns),
        ._pattern = 0,  // placeholder, overwritten
    };
}

template<>
RevGuiPatternIter RevGuiPatternIter::from_beat(
    doc::TimelineChannelRef timeline, GridAndBeat now
) {
    auto iter = FrameIter(timeline[now.grid]);

    std::vector<doc::PatternRef> cell_patterns;

    // Include all patterns, except those starting after now.
    while (auto pat = iter.next()) {
        if (pat->begin_time > now.beat) {
            break;
        }
        cell_patterns.push_back(*pat);
    }

    return RevGuiPatternIter {
        ._timeline = timeline,

        ._grid = now.grid,
        ._cell_patterns = cell_patterns,
        ._pattern = 0,  // placeholder, overwritten
    };
}

/// Would it work better as an actual state machine? Maybe, I don't know.
/// I don't know if it's better to separate out the "first call, invalid block"
/// and "subsequent call" logic.
template<>
std::optional<GuiPatternIterItem> FwdGuiPatternIter::next() {
    scrBegin;

    release_assert(_grid < _timeline.size());
    goto begin;

    for (; _grid < _timeline.size(); _grid++) {
        {
            _cell_patterns.clear();

            auto cell_iter = FrameIter(_timeline[_grid]);
            while (auto p = cell_iter.next()) {
                _cell_patterns.push_back(*p);
            }
        }

        begin:
        DEBUG_PRINT("forward, patterns size {}\n", _cell_patterns.size());
        for (_pattern = 0; _pattern < _cell_patterns.size(); _pattern++) {
            scrBeginScope;
            doc::PatternRef pattern = _cell_patterns[_pattern];

            DEBUG_PRINT(
                "forward, grid {}, time {} to {}\n",
                _grid,
                pattern.begin_time,
                format_frac(pattern.end_time)
            );
            scrReturnEndScope((
                GuiPatternIterItem{_grid, pattern}
            ));
        }
    }
    while (true) {
        DEBUG_PRINT("forward, nullopt\n");
        scrReturn(std::nullopt);
    }

    scrFinishUnreachable;
    throw std::logic_error("Reached end of FwdTrackPatternWrap::next()");
}

template<>
std::optional<GuiPatternIterItem> RevGuiPatternIter::next() {
    scrBegin;

    release_assert(_grid < _timeline.size());
    goto begin;

    if (_timeline.size())
    while (true) {
        {
            _cell_patterns.clear();

            auto cell_iter = FrameIter(_timeline[_grid]);
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

            DEBUG_PRINT(
                "reverse, grid {}, time {} to {}\n",
                _grid,
                pattern.begin_time,
                format_frac(pattern.end_time)
            );
            scrReturnEndScope((
                GuiPatternIterItem{_grid, pattern}
            ));

            if (_pattern == 0) break;
            _pattern--;
        }

        if (_grid == GridIndex(0)) break;
        _grid--;
    }
    while (true) {
        DEBUG_PRINT("reverse, nullopt\n");
        scrReturn(std::nullopt);
    }

    scrFinishUnreachable;
    throw std::logic_error("Reached end of RevTrackPatternWrap::next()");
}

template class detail::GuiPatternIter<detail::Direction::Reverse>;
template class detail::GuiPatternIter<detail::Direction::Forward>;

}
