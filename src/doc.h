#pragma once

/// Patterns contain rows at times (TimeInPattern).
/// TimeInPattern contains both a fractional anchor beat, and an offset in frames.
/// Rows can contain notes, effects, or both.

#include "doc/events.h"
#include "doc/timed_events.h"
#include "doc/event_list.h"
#include "doc/timeline.h"
#include "doc/instr.h"
#include "doc/accidental_common.h"
#include "chip_common.h"
#include "util/copy_move.h"

#include <vector>

namespace doc {

// Re-export
using namespace ::doc::events;
using namespace ::doc::timed_events;
using namespace ::doc::event_list;
using namespace ::doc::timeline;
using namespace ::doc::instr;
using accidental::AccidentalMode;

constexpr TickT MAX_TICKS_PER_BEAT = 127;

struct SequencerOptions {
    TickT ticks_per_beat;
};

// Tuning table types
inline namespace tuning {
    using ChromaticInt = ChromaticInt;
    using FreqDouble = double;
    using RegisterInt = int;

    template<typename T>
    using Owned_ = std::vector<T>;

    template<typename T>
    using Ref_ = gsl::span<T const, CHROMATIC_COUNT>;

    using FrequenciesOwned = Owned_<FreqDouble>;
    using FrequenciesRef = Ref_<FreqDouble>;

    using TuningOwned = Owned_<RegisterInt>;
    using TuningRef = Ref_<RegisterInt>;

    FrequenciesOwned equal_temperament(
        ChromaticInt root_chromatic = 69, FreqDouble root_frequency = 440.
    );
}

using chip_kinds::ChipKind;
using ChipList = std::vector<chip_kinds::ChipKind>;

/// Document struct.
///
/// Usage:
/// You can construct a DocumentCopy (not Document)
/// via aggregate initialization or designated initializers.
/// Afterwards, convert to Document to avoid accidental expensive copies.
struct DocumentCopy {
    SequencerOptions sequencer_options;
    FrequenciesOwned frequency_table;
    AccidentalMode accidental_mode;

    Instruments instruments;

    /// vector<ChipIndex -> ChipKind>
    /// chips.size() in [1..MAX_NCHIP] inclusive (not enforced yet).
    ChipList chips;

    /// The order editor is replaced with a global timeline grid for placing patterns in.
    /// We don't store the absolute times of gridlines,
    /// but instead the distance between them.
    ///
    /// Grid cells lie between gridlines.
    /// Each cell has a duration consisting of an integer(?) number of beats.
    /// This variable stores cell durations, not block/pattern data.
    /// ----
    /// Timeline cell `i` has duration `grid_cells[i].nbeats`.
    /// Valid gridlines are `0 .. grid_cells.size()` inclusive.
    /// However, the last grid cell is at size() - 1.
    GridCells grid_cells;

    /// This variable stores document pattern data.
    ChipChannelTo<Timeline> chip_channel_timelines;

    // Methods
    chip_common::ChannelIndex chip_index_to_nchan(chip_common::ChipIndex index) const {
        return chip_common::CHIP_TO_NCHAN[(size_t)chips[index]];
    }
};

/// Non-copyable version of Document. You must call clone() explicitly.
struct Document : DocumentCopy {
    Document clone() const;

    // Document(Document.clone())
    Document(DocumentCopy const & other);
    Document(DocumentCopy && other);

private:
    DEFAULT_COPY(Document)

public:
    DEFAULT_MOVE(Document)
};

// namespace doc
}
