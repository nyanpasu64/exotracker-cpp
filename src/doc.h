#pragma once

/// Patterns contain rows at times (TimeInPattern).
/// TimeInPattern contains both a fractional anchor beat, and an offset in frames.
/// Rows can contain notes, effects, or both.

#include "doc/events.h"
#include "doc/timed_events.h"
#include "doc/event_list.h"
#include "doc/sequence.h"
#include "doc/instr.h"
#include "chip_common.h"
#include "util/copy_move.h"

#include <vector>

namespace doc {

// Re-export
using namespace ::doc::events;
using namespace ::doc::timed_events;
using namespace ::doc::event_list;
using namespace ::doc::sequence;
using namespace ::doc::instr;

struct SequencerOptions {
    TickT ticks_per_beat;
};

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

    Instruments instruments;

    /// vector<ChipIndex -> ChipKind>
    /// chips.size() in [1..MAX_NCHIP] inclusive (not enforced yet).
    ChipList chips;

    // Sequence.size() in [1..MAX_SEQUENCE_LEN] inclusive (not enforced yet).
    Sequence sequence;

    // Methods
    chip_common::ChannelIndex chip_index_to_nchan(chip_common::ChipIndex index) const {
        return chip_common::CHIP_TO_NCHAN[(size_t)chips[index]];
    }
};

/// Non-copyable version of Document. You must call clone() explicitly.
struct Document : DocumentCopy {
    Document clone() const {
        return *this;
    }

    // Document(Document.clone())
    Document(DocumentCopy const & other) : DocumentCopy(other) {}
    Document(DocumentCopy && other) : DocumentCopy(std::move(other)) {}

private:
    DEFAULT_COPY(Document)

public:
    DEFAULT_MOVE(Document)
};

// namespace doc
}
