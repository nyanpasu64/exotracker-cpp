#pragma once

#include "doc.h"
#include "util/copy_move.h"
#include "util/macros.h"

#include <cstdint>
#include <optional>

namespace audio::synth::envelope {

/// EnvelopeT = doc::instr::Envelope<some IntT>.
template<typename EnvelopeT>
class EnvelopeIterator {
    using IntT = typename EnvelopeT::IntT;

    /// which envelope to track (volume, pitch, arpeggio...)
    using EnvelopePtr = EnvelopeT doc::Instrument::*;
    EnvelopePtr /*const*/ _field;

    // Treat "no instrument loaded" as "instrument loaded, all envelopes empty".

    /// Value if we trigger a new note, but instrument envelope is empty.
    IntT /*const*/ _default_value;

    /// Which instrument to track.
    std::optional<doc::InstrumentIndex> _curr_instr;

    /// Value from previous call to next().
    IntT _prev_value = 0;
    // _prev_value initialization doesn't matter.
    // when _next_position is None, _prev_value is not accessed.
    // when _next_position is Some, it must've been set by trigger(),
    // which initializes _prev_value as well.

    /// Time index into envelope.
    std::optional<uint32_t> _next_position;

public:
    EnvelopeIterator(EnvelopePtr field, IntT default_value) noexcept :
        _field(field),
        _default_value(default_value),
        _curr_instr(),
        _next_position()
        // On MSVC and Clang, optional_field({}) initializes to 0.
    {}

    explicit DEFAULT_COPY(EnvelopeIterator)
    DEFAULT_MOVE(EnvelopeIterator)

    /*
    state machine

    after note cut: _next_position = {}, next() should output zeros.
    after attack: _next_position ∈ [0, instr.release)
    after release: _next_position ∈ [instr.release, instr.len)
    */

private:
    bool is_playing() {
        return _next_position.has_value();
    }

TEST_PUBLIC:
    static const EnvelopeT _empty_env;

private:
    /// If _curr_instr loaded, use it. Otherwise, use empty envelope.
    /// next() should never branch or index using _curr_instr directly.
    EnvelopeT const & extract_env(doc::Document const & document) {
        if (_curr_instr.has_value() && *_curr_instr < document.instruments.v.size()) {
            auto const & instr = document.instruments[*_curr_instr];
            if (instr.has_value()) {
                return (*instr).*_field;
            }
        }
        return _empty_env;
    }

    // Alters _next_position.
    void trigger() {
        _next_position = 0;
        _prev_value = _default_value;
    }

public:
    // Calling these functions signals events occurring at time _next_position.
    // These functions do not increase _next_position.

    /// note with/without instrument
    void note_on(std::optional<doc::InstrumentIndex> const instrument) {
        // specify an instrument once, future instrument-free notes keep using it.
        if (instrument.has_value()) {
            _curr_instr = instrument;
        }
        // new note? always trigger.
        trigger();
    }

    /// instrument command with no note
    void switch_instrument(doc::InstrumentIndex const instrument) {
        // in 0cc, same instrument doesn't retrigger. only different does.
        if (_curr_instr != instrument) {
            _curr_instr = {instrument};
            trigger();
        }
    }

TEST_PUBLIC:
    // Alters _next_position.
    void release_raw(EnvelopeT const & env) {
        if (!is_playing()) {
            return;
        }

        // how to handle instruments without a release set?
        // do nothing? jump to end? jump to 0 volume?
//        if (env.release.has_value()) {
//            _next_position = *env.release;
//        }
    }

public:
    void release(doc::Document const & document) {
        release_raw(extract_env(document));
    }

    // Alters _next_position.
    void note_cut() {
        _next_position = {};
    }

TEST_PUBLIC:
    IntT next_raw(EnvelopeT const & env) {
        if (!is_playing()) {
            return 0;
        }

        // _next_position.has_value() true.
        if (*_next_position >= env.values.size()) {
            return _prev_value;
        }

        _prev_value = env.values[*_next_position];
        // if (_next_position + 1 < env.release)
        {
            (*_next_position)++;
        }

        return _prev_value;
    }

public:
    /// Call next() after calling the other methods.
    ///
    /// Precondition:
    ///   _next_position = t
    /// Return:
    ///   value at [t, t+1)
    /// Postcondition:
    ///   _next_position = t+1
    IntT next(doc::Document const & document) {
        return next_raw(extract_env(document));
    }
};

template<typename EnvelopeT>
const EnvelopeT EnvelopeIterator<EnvelopeT>::_empty_env = EnvelopeT::new_empty();

}
