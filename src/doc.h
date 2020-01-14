#pragma once

/// Patterns contain rows at times (TimeInPattern).
/// TimeInPattern contains both a fractional anchor beat, and an offset in frames.
/// Rows can contain notes, effects, or both.
///
/// We use the C++ Immer library to implement immutable persistent data structures.
///
/// According to https://sinusoid.es/immer/containers.html#_CPPv2NK5immer6vectorixE9size_type ,
/// indexing into an Immer container returns a `const &`,
/// which becomes mutable when copied to a local.
/// Therefore when designing immer::type<Inner>, Inner can be a mutable struct,
/// std collection, or another immer::type.

#include "doc/events.h"
#include "doc/timed_events.h"
#include "doc/event_list.h"
#include "doc/sequence.h"
#include "chip_kinds.h"
#include "util/copy_move.h"

#include <vector>

namespace doc {

// Re-export
using namespace ::doc::events;
using namespace ::doc::timed_events;
using namespace ::doc::event_list;
using namespace ::doc::sequence;

struct SequencerOptions {
    TickT ticks_per_beat;
};

struct DocumentCopy {
    /// vector<ChipIndex -> ChipKind>
    /// chips.size() in [1..MAX_NCHIP] inclusive (not enforced yet).
    using ChipList = std::vector<chip_kinds::ChipKind>;
    ChipList chips;

    chip_kinds::ChannelIndex chip_index_to_nchan(chip_kinds::ChipIndex index) const {
        return chip_kinds::CHIP_TO_NCHAN[chips[index]];
    }

    // TODO add multiple patterns.
    SequenceEntry pattern;

    SequencerOptions sequencer_options;
    FrequenciesOwned frequency_table;
};

struct Document : DocumentCopy {
    // Expose clone, with explicit syntax.
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

Document dummy_document();

/// get_document() must be thread-safe in implementations.
/// For example, if implemented by DocumentHistory,
/// get_document() must not return invalid states while undoing/redoing.
class GetDocument {
public:
    virtual ~GetDocument() = default;
    virtual Document const & get_document() const = 0;
};

// namespace doc
}
