#include "edit_doc.h"
#include "edit_impl.h"
#include "doc.h"
#include "util/typeid_cast.h"
#include "util/release_assert.h"

#include <variant>
#include <utility>  // std::swap

namespace edit::edit_doc {

using namespace edit_impl;

/// type GetFieldMut<T> = fn(&mut Document) -> &mut T;
template<typename T>
using GetFieldMut = T & (*)(doc::Document &);


template<typename T, GetFieldMut<T> get_field_mut, ModifiedFlags modified>
class Setter {
    T _value;

public:
    static constexpr ModifiedFlags _modified = modified;

    Setter(T new_value)
        : _value(new_value)
    {}

    void apply_swap(doc::Document & document) {
        std::swap(get_field_mut(document), _value);
    }

    using Impl = ImplEditCommand<Setter, Override::CanMerge>;
    bool can_merge(BaseEditCommand & prev) const {
        // TODO when pushing edits, freeze previous commands to prevent merging.
        // Currently, if you undo to after a spinbox edit, and spin it again,
        // the previous undo state is destroyed!

        return typeid(prev) == typeid(Impl);
    }
};

/// I wanted to turn this function into a local-variable lambda,
/// but in GCC, local-variable lambdas cannot be used as
/// non-type template parameters (Setter<GetFieldMut<T>>).
/// Context: https://docs.google.com/document/d/1D5lKC6eFOv4fptVkUsk8DgeboFSBigSQWef8-rqCzgw/edit
///
/// Honestly I'm disappointed you can't pass a "path" (document => document.foo.bar)
/// as a first-class template parameter.
/// Though Rust lambdas are quite concise (|d| &mut d.foo.bar).
static double & get_tempo_mut(doc::Document & document) {
    return document.sequencer_options.target_tempo;
};

EditBox set_tempo(double tempo) {
    return make_command(Setter<double, get_tempo_mut, ModifiedFlags::EngineTempo>(
        tempo
    ));
}

static int& get_measure_len_mut(doc::Document & document) {
    return document.sequencer_options.beats_per_measure;
};


EditBox set_beats_per_measure(int measure_len) {
    return make_command(Setter<int, get_measure_len_mut, (ModifiedFlags) 0>(
        measure_len
    ));
}

using doc::Document;
using doc::SequencerOptions;

class SetSequencerOptions {
    SequencerOptions _value;

public:
    ModifiedFlags _modified;

    SetSequencerOptions(SequencerOptions new_value, ModifiedFlags modified)
        : _value(new_value)
        , _modified(modified)
    {}

    void apply_swap(doc::Document & document) {
        std::swap(document.sequencer_options, _value);
    }

    using Impl = ImplEditCommand<SetSequencerOptions, Override::None>;
};

EditBox set_sequencer_options(
    Document const& orig_doc, SequencerOptions options
) {
    ModifiedInt flags = 0;
    {
        auto const& orig_options = orig_doc.sequencer_options;

#define CHANGED(FIELD)  (options.FIELD != orig_options.FIELD)

        // These parameters are used to calculate engine tempo; set the flag if any
        // changed.
        if (
            CHANGED(target_tempo) ||
            CHANGED(ticks_per_beat) ||
            CHANGED(spc_timer_period))
        {
            flags |= ModifiedFlags::EngineTempo;
        }

        // Not worth adding a flag for CHANGED(note_gap_ticks). Even if the sequencer
        // doesn't handle it changing, the worst thing that can happen is that notes
        // release 2 ticks later than they should, causing a momentary pop upon the
        // next note.
        //
        // CHANGED(beats_per_measure) only affects the pattern editor, not the
        // sequencer; don't set a flag.
    }

    return make_command(SetSequencerOptions(options, ModifiedFlags{flags}));
}

}
