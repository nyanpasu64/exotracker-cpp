#pragma once

#include "event_list.h"
#include "timed_events.h"
#include "doc_common.h"  // DenseMap
#include "chip_common.h"

#include <optional>

namespace doc::sequence {

using chip_common::ChipIndex;
using chip_common::ChannelIndex;

template<typename V>
using ChipChannelTo = DenseMap<ChipIndex, DenseMap<ChannelIndex, V>>;

template<typename V>
using ChannelTo = DenseMap<ChannelIndex, V>;

/// Represents the contents of one row in the sequence editor.
/// Like FamiTracker, Exotracker will use a pattern system
/// where each sequence row contains [for each channel] pattern ID.
///
/// The list of [chip/channel] [pattern ID] -> pattern data is stored separately
/// (in PatternStore).
struct SequenceEntry {
    /// Invariant: Must be positive and nonzero.
    timed_events::BeatFraction nbeats;

    // TODO add pattern indexing scheme.
    /**
    Invariant (expressed through dependent types):
    - [chip: ChipInt] [ChannelID<chips[chip]: ChipKind>] EventList

    Invariant (expressed without dependent types):
    - chip: (ChipInt = [0, Document.chips.size()))
    - chips[chip]: ChipKind
    - channel: (ChannelIndex = [0, CHIP_TO_NCHAN[chip]))
    - chip_channel_events[chip][channel]: PatternID = pattern_id

    PatternStore:
    - chip_channel_pattern_store[chip][channel][pattern_id]: EventList
    - All unused pattern_id are filled with empty EventList.
    */
    ChipChannelTo<event_list::EventList> chip_channel_events;
};

using SeqEntryIndex = uint32_t;

using MaybeSeqEntryIndex = std::optional<SeqEntryIndex>;

// SequencerTime::seq_entry_index is 16 bits, but all-1 is reserved.
// The actual driver will probably be restricted to 255-256 sequence entries.
constexpr SeqEntryIndex MAX_SEQUENCE_LEN = 256;

/// [SequenceIndex] SequenceEntry
using Sequence = DenseMap<SeqEntryIndex, SequenceEntry>;

}
