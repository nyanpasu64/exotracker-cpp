#pragma once

/// Thin wrapper around doc_util/track_util (iterating over patterns in a
/// SequenceTrack). Currently behaves similarly to TrackPatternIter; I may re-add
/// wrapping at some point.

#include "doc.h"
#include "doc_util/track_util.h"
#include "timing_common.h"

#include <tuple>

namespace gui::gui_time {

// Public re-export.
using namespace doc_util::track_util;

using doc::ChipIndex;
using doc::ChannelIndex;
using doc::TickT;
using doc::PatternRef;

// # Iterating over SequenceTrack (only used in move_cursor.cpp).

using GuiPatternIterItem = doc::PatternRef;

namespace detail {
    enum class Direction {
        Forward,
        Reverse,
    };

    /// Returns the pattern the cursor lies within (if any), then patterns before or
    /// after the cursor (based on `direction`).
    ///
    /// Does not allocate memory. Currently used for moving cursor to the next event
    /// (which may be on the current pattern, next, or even further).
    template<Direction direction>
    class GuiPatternIter {
    public:
        // Please don't poke this class's fields.
        // I'm marking them public to silence Clang warnings.
        TrackPatternIterRef _iter;

        // impl
    public:

        /// Returns the pattern the cursor lies within (if any), then patterns before
        /// or after the cursor (based on `direction`).
        [[nodiscard]] static
        GuiPatternIter from_time(doc::SequenceTrackRef track, TickT now);

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
