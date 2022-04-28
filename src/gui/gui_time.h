#pragma once

/// Utility functions for searching through TimelineCell
/// and extracting patterns from Timeline.

#include "doc.h"
#include "doc_util/time_util.h"
#include "timing_common.h"

#include <coroutine.h>

#include <tuple>

namespace gui::gui_time {

// Public re-export.
using namespace doc_util::time_util;

using doc::ChipIndex;
using doc::ChannelIndex;

using doc::GridIndex;
using doc::BeatFraction;

using timing::GridAndBeat;
using timing::GridBlockBeat;
using timing::GridAndBlock;


#if 0
/// Unused.
/// Convert a block-relative timestamp to a grid timestamp.
///
/// Adds the block's begin time and the relative timestamp's beat offset.
/// Then computes the correct grid cell containing the time.
[[nodiscard]] GridAndBeat real_time(
    doc::Document const& document,
    ChipIndex chip,
    ChannelIndex channel,
    GridBlockBeat rel_time
);
#endif


// # Iterating over Timeline (only used in move_cursor.cpp).

struct GuiPatternIterItem {
    GridIndex grid;
    doc::PatternRef pattern;
};

namespace detail {
    enum class Direction {
        Forward,
        Reverse,
    };

    /// Allocates memory, cannot be used on audio thread.
    ///
    /// Currently used for moving cursor to the next event
    /// (which may be on the current pattern, next, or even further).
    template<Direction direction>
    class GuiPatternIter {
    public:
        // Please don't poke this class's fields.
        // I'm marking them public to silence Clang warnings.
        scrDefine;
        doc::TimelineChannelRef _timeline;

        GridIndex _grid;
        std::vector<doc::PatternRef> _cell_patterns;
        size_t _pattern;

        // impl
    public:

        [[nodiscard]] static
        GuiPatternIter from_beat(doc::TimelineChannelRef timeline, GridAndBeat now);

        /// First call:
        /// - If original state is valid, return it as-is.
        /// - If original state is invalid, search the document for the first valid block.
        ///   If none exist, enter nullopt state.
        /// Subsequent calls:
        /// - Return the next block. At begin/end of document, enter nullopt state.
        /// Nullopt state:
        /// - Return nullopt.
        [[nodiscard]] std::optional<GuiPatternIterItem> next();
    };
}

// Do you need specialization *and* explicit instantiation?
using RevGuiPatternIter = detail::GuiPatternIter<detail::Direction::Reverse>;
using FwdGuiPatternIter = detail::GuiPatternIter<detail::Direction::Forward>;

}
