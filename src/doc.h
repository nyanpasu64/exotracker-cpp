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

#include <gsl/span>

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
    /// Usually 48.
    TickT ticks_per_beat;

    /// BPM tempo. The module will play at approximately this tempo,
    /// with rounding and range dependent on ticks_per_beat.
    double beats_per_minute;

    /// Use the tempo exactly (or a close approximation)
    /// instead of rounding to the SNES timer precision.
    /// Turning this on causes in-tracker playback and SPC export to diverge.
    bool use_exact_tempo = false;
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
    using TuningRef = Ref_<RegisterInt const>;

    FrequenciesOwned equal_temperament(
        ChromaticInt root_chromatic = 69, FreqDouble root_frequency = 440.
    );
}

using chip_kinds::ChipKind;
using ChipList = std::vector<chip_kinds::ChipKind>;

struct ChannelSettings {
    events::EffColIndex n_effect_col = 1;
};

using ChipChannelSettings = ChipChannelTo<ChannelSettings>;

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

    /// Whether effect names are 1 or 2 characters wide.
    /// When set to 1, the first digit is hidden if it's 0,
    /// and typing character c will write effect 0c immediately.
    uint32_t effect_name_chars = 1;

    Instruments instruments;

    /// vector<ChipIndex -> ChipKind>
    /// chips.size() in [1..MAX_NCHIP] inclusive (not enforced yet).
    ChipList chips;

    ChipChannelSettings chip_channel_settings;

    /// The order editor is replaced with a global timeline grid for placing patterns in.
    /// We don't store the absolute times of gridlines,
    /// but instead the distance between them.
    ///
    /// Grid cells lie between gridlines.
    /// Each cell has a duration consisting of an integer(?) number of beats.
    /// This variable stores both cell durations and block/pattern data.
    /// ----
    /// Timeline cell `i` has duration `timeline[i].nbeats`.
    /// Valid gridlines are `0 .. timeline.size()` inclusive.
    /// However, the last grid cell is at size() - 1.
    Timeline timeline;

// impl
    [[nodiscard]] chip_common::ChannelIndex chip_index_to_nchan(
        chip_common::ChipIndex chip
    ) const;
};

/// Non-copyable version of Document. You must call clone() explicitly.
struct Document : DocumentCopy {
    Document clone() const;

    Document(DocumentCopy const & other);
    Document(DocumentCopy && other);

    DISABLE_COPY(Document)
    DEFAULT_MOVE(Document)
};

// namespace doc
}
