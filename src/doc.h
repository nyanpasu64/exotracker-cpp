#pragma once

/// Patterns contain rows at times (TimeInPattern).
/// TimeInPattern contains both a fractional anchor beat, and an offset in frames.
/// Rows can contain notes, effects, or both.

#include "doc/events.h"
#include "doc/timed_events.h"
#include "doc/event_list.h"
#include "doc/timeline.h"
#include "doc/sample.h"
#include "doc/instr.h"
#include "doc/accidental_common.h"
#include "chip_common.h"
#include "util/box_array.h"
#include "util/copy_move.h"

#ifdef UNITTEST
#include "util/compare.h"
#endif

#include <gsl/span>

#include <vector>

namespace doc {

// Re-export
using namespace ::doc::events;
using namespace ::doc::timed_events;
using namespace ::doc::event_list;
using namespace ::doc::timeline;
using namespace ::doc::sample;
using namespace ::doc::instr;
using accidental::AccidentalMode;

using util::box_array::BoxArray;

/// The sound engine is driven by the S-SMP timer, which runs at a high frequency
/// (8010 Hz / `spc_timer_period`), fixed per-game and not changing with song tempo.
/// The sequencer only gets ticked (advancing document playback and triggering notes)
/// on a fraction of these timer events, determined by the "sequencer rate" value
/// (not saved in document, but computed from tempo).
///
/// (Note that the S-SMP timer's base frequency varies between consoles
/// because it uses a cheap ceramic resonator as a frequency source.
/// It is nominally 8000 Hz, but is higher in practice, on average 8010 Hz or more.)
///
/// The user specifies a `target_tempo` (in BPM), which gets converted into an
/// "sequencer rate" upon in-tracker playback or SPC export.
/// The song playback rate (in BPM) is determined by the "sequencer rate",
/// as well as `spc_timer_period` and `ticks_per_beat` (specified by the user).
///
/// What is the conversion formula to calculate the best "sequencer rate"
/// for a target tempo?
/// Let t = `target_tempo`, d = `spc_timer_period`, r = "sequencer rate", p = `ticks_per_beat`.
/// To compute the appropriate "sequencer rate" for a given tempo,
/// solve for r in terms of t.
///
///     t = (8010 timers / d s) * (r ticks / 256 timers) * (1 beat / p ticks) * (60 s / min)
///     t = (8010*60/256)r/dp beat/min
///     (dp*256/60/8010)t = r
///
/// The default values of d=16 and p=48 (taken from AMK) results in r ≈ 0.4091*t. As a
/// result, the only achievable tempos are multiples of around 2.5 BPM.
///
/// Increasing `ticks_per_beat` makes note timing and tempo more fine-grained, but
/// makes exported .spcs more likely to lag.
///
/// Increasing `spc_timer_period` increases the per-tick clock budget (making exported
/// .spcs less likely to lag) and makes tempo more fine-grained, but increases note
/// timing error as well.
struct SequencerOptions {
    /// The target tempo to play the module at, in beats/minute. Controls the
    /// percentage of timer ticks that trigger sequencer ticks. Note that the actual
    /// playback tempo will not match this value exactly (due to rounding), and note
    /// times will jitter slightly as well (increasing spc_timer_period increases note
    /// jitter).
    double target_tempo;

    /// How many sequencer ticks before a new note/rest to release the previous note.
    /// This creates a gap between notes, but allows the previous note to fade to
    /// silence instead of being interrupted by the next note creating a pop.
    ///
    /// Increasing target_tempo or ticks_per_beat reduces the duration of each
    /// sequencer tick. Increasing spc_timer_period increases the jitter of each
    /// sequencer tick.
    ///
    /// 2 by default on AMK, 1 with "light staccato" enabled.
    TickT note_gap_ticks = 1;

    /// The scaling factor used to convert *all* BPM tempos into sequencer tempos; also
    /// determines *initial* visual beat length in the pattern editor.
    ///
    /// Defaults to 48 (the value used in Square's SPC drivers, including FF6, as well
    /// as AMK). Change to 36 or 72 for 6/8 songs.
    TickT ticks_per_beat = 48;

    /// Purely cosmetic; determines the initial visual measure length in the pattern
    /// editor.
    int beats_per_measure = 4;

    /// Controls the period of the SPC timer, which controls when the engine advances.
    /// Increasing this value causes the driver to run less often.
    /// This increases the amount of note timing jitter, but decreases the likelihood
    /// of driver slowdown (taking too long to run and falling behind).
    ///
    /// Valid values range from [1 .. 256] inclusive.
    /// The value will be written into the SNES S-SMP timer divisor address ($00fa),
    /// except 256 (0x100) will be written as 0 instead (which acts as 256).
    ///
    /// Defaults to 16 which runs the engine at 500 Hz, matching N-SPC and AddMusicK.
    /// This results in low tempo precision, plus slowdown in busy sections of music.
    /// Increasing this value improves tempo precision and reduces lag, but reduces
    /// note and vibrato time resolution. Values of 39-45 may work well.
    uint32_t spc_timer_period = 16;

// impl
#ifdef UNITTEST
    DEFAULT_EQUALABLE(SequencerOptions)
#endif
};

constexpr double MIN_TEMPO = 1.;
constexpr double MAX_TEMPO = 999.;

constexpr uint32_t MIN_TIMER_PERIOD = 1;
constexpr uint32_t MAX_TIMER_PERIOD = 256;

constexpr TickT MIN_TICKS_PER_BEAT = 1;
constexpr TickT MAX_TICKS_PER_BEAT = 192;

// Tuning table types
inline namespace tuning {
    using Chromatic = Chromatic;
    using FreqDouble = double;
    using RegisterInt = int;

    /// .size() must be CHROMATIC_COUNT.
    template<typename T>
    using Owned_ = BoxArray<T, CHROMATIC_COUNT>;

    template<typename T>
    using Ref_ = gsl::span<T const, CHROMATIC_COUNT>;

    using FrequenciesOwned = Owned_<FreqDouble>;
    using FrequenciesRef = Ref_<FreqDouble>;

    using TuningOwned = Owned_<RegisterInt>;
    using TuningRef = Ref_<RegisterInt const>;

    FrequenciesOwned equal_temperament(
        Chromatic root_chromatic = 69, FreqDouble root_frequency = 440.
    );

    inline constexpr double MIN_TUNING_FREQ = 0.;
    inline constexpr double MAX_TUNING_FREQ = 1'000'000.;
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

    /// Whether effect names are 1 or 2 characters wide.
    /// When set to 1, the first digit is hidden if it's 0,
    /// and typing character c will write effect 0c immediately.
    uint8_t effect_name_chars = 1;

    Samples samples;
    Instruments instruments;

    /// vector<ChipIndex -> ChipKind>
    /// chips.size() in [1..MAX_NCHIP] inclusive (not enforced yet).
    ChipList chips;

    Sequence sequence;

// impl
    [[nodiscard]] chip_common::ChannelIndex chip_index_to_nchan(
        chip_common::ChipIndex chip
    ) const;

#ifdef UNITTEST
    DEFAULT_EQUALABLE(DocumentCopy)
#endif
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
