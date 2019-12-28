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
#include "audio/synth/chip_kinds_common.h"

#include <immer/array.hpp>
#include <immer/array_transient.hpp>

namespace doc {

// Re-export
using namespace ::doc::events;
using namespace ::doc::timed_events;
using namespace ::doc::event_list;
using namespace ::doc::sequence;

namespace chip_kinds = audio::synth::chip_kinds;

struct SequencerOptions {
    TickT ticks_per_beat;
};

struct Document {
    /// vector<ChipIndex -> ChipKind>
    /// chips.size() in [1..MAX_NCHIP] inclusive (not enforced yet).
    using ChipList = immer::array<chip_kinds::ChipKind>;
    ChipList chips;

    chip_kinds::ChannelIndex chip_index_to_nchan(chip_kinds::ChipIndex index) const {
        return chip_kinds::CHIP_TO_NCHAN[chips[index]];
    }

    // TODO add multiple patterns.
    SequenceEntry pattern;

    SequencerOptions sequencer_options;
    FrequenciesOwned frequency_table;
};

Document dummy_document();

struct HistoryFrame {
    Document document;
    // TODO add std::string diff_from_previous.
};

/// get_document() must be thread-safe in implementations.
/// For example, if implemented by DocumentHistory,
/// get_document() must not return invalid states while undoing/redoing.
class GetDocument {
public:
    virtual ~GetDocument() = default;
    virtual Document const & get_document() const = 0;
};

// immer::flex_vector (possibly other types)
// is a class wrapping immer/detail/rbts/rrbtree.hpp.
// immer's rrbtree is the size of a few pointers, and does not hold node data.
// So immer types take up little space in their owning struct (comparable to shared_ptr).

// namespace doc
}
