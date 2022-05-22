#include "gui_time.h"
#include "util/enumerate.h"
#include "util/format.h"
#include "util/release_assert.h"

#include <limits>
#include <stdexcept>

//#define TIME_CONV_DEBUG

#ifdef TIME_CONV_DEBUG
    #include <fmt/core.h>
    #define DEBUG_PRINT(...)  fmt::print(stderr, __VA_ARGS__)
#else
    #define DEBUG_PRINT(...)
#endif

namespace gui::gui_time {


// It might be useful for GuiPatternIter prev/next to return a different first value
// if now is between blocks. But that'll be done later.
template<>
FwdGuiPatternIter FwdGuiPatternIter::from_time(
    doc::SequenceTrackRef track, TickT now
) {
    auto x = TrackPatternIterRef::at_time(track, now);
    return FwdGuiPatternIter {
        ._iter = x.iter,
    };
}

template<>
RevGuiPatternIter RevGuiPatternIter::from_time(
    doc::SequenceTrackRef track, TickT now
) {
    auto x = TrackPatternIterRef::at_time(track, now);
    if (x.snapped_later) {
        x.iter.prev();
    }

    return RevGuiPatternIter {
        ._iter = x.iter,
    };
}

/// Would it work better as an actual state machine? Maybe, I don't know.
/// I don't know if it's better to separate out the "first call, invalid block"
/// and "subsequent call" logic.
template<>
std::optional<GuiPatternIterItem> FwdGuiPatternIter::next() {
    if (auto pattern = _iter.peek()) {
        _iter.next();
        return *pattern;
    }
    return {};
}

template<>
std::optional<GuiPatternIterItem> RevGuiPatternIter::next() {
    if (auto pattern = _iter.peek()) {
        _iter.prev();
        return *pattern;
    }
    return {};
}

template class detail::GuiPatternIter<detail::Direction::Reverse>;
template class detail::GuiPatternIter<detail::Direction::Forward>;

}
