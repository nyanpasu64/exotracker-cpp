#include "edit_instr.h"
#include "edit_impl.h"
#include "util/release_assert.h"
#include "util/compare.h"
#include "util/typeid_cast.h"

#include <utility>  // std::declval

namespace edit::edit_instr {

using namespace doc;
using namespace edit_impl;

/*
TODO the current SetKeysplit/PatchSetter division
screws with the ability to merge undo commands.
Currently all PatchSetter to the same instrument and patch merge,
while all SetKeysplit to the same instrument with _can_merge=true merge.

Additionally, merging is a footgun because it's easy to accidentally return true
to two edits of the same type to different indexes in the document.

Hopefully, we can decide on a better way of deciding
which undo commands to merge or not,
and decouple the type of edit command from whether it can be merged.

See https://docs.google.com/document/d/15aI6Y84rvki-VqljTmqx4nbV-fNhzPQA4dy-LJboJww/edit
for details.
*/

// Keysplit edits which add, remove, or reorder patches.

/// Adding or removing a patch replaces the instrument's entire keysplit.
/// The advantage is that SetKeysplit can merge with each other when desired.
/// The disadvantage is that if a user adds 128 patches
/// and then repeatedly removes/adds/reorders patches,
/// storing the entire keysplit in each edit wastes RAM in the undo history.
/// I don't care, because it's unlikely for a user to add so many patches,
/// and it wastes less RAM than a user adding hundreds/thousands of events
/// to a single pattern and then repeatedly editing it.
///
/// We could alternatively insert or remove a single patch,
/// but that requires reserving MAX_KEYSPLITS (128) items
/// in each instrument's keysplit in the audio thread,
/// both when sending over a document and when inserting instruments later on.
/// Reserving memory eats RAM even if you never add that many patches,
/// and is easy to forget to pre-allocate memory.
/// Additionally it's harder to implement and can't merge.
struct SetKeysplit {
    InstrumentIndex _instr_idx;
    std::vector<InstrumentPatch> _keysplit;
    bool _can_merge;

// impl
    SetKeysplit(
        InstrumentIndex instr_idx,
        std::vector<InstrumentPatch> keysplit,
        bool can_merge = false)
    :
        _instr_idx(instr_idx)
        , _keysplit(std::move(keysplit))
        , _can_merge(can_merge)
    {}

    void apply_swap(doc::Document & doc) {
        auto & maybe_instr = doc.instruments[_instr_idx];
        release_assert(maybe_instr);
        std::swap(maybe_instr->keysplit, _keysplit);
    }

    using Impl = ImplEditCommand<SetKeysplit, Override::CanMerge>;
    bool can_merge(BaseEditCommand & prev) const {
        if (auto p = typeid_cast<Impl *>(&prev)) {
            return p->_instr_idx == _instr_idx
                && p->_can_merge
                && _can_merge;
        }
        return false;
    }

    static constexpr ModifiedFlags _modified = ModifiedFlags::InstrumentsEdited;
};

MaybeEditBox try_add_patch(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx
) {
    release_assert(instr_idx < doc.instruments.size());
    auto & maybe_instr = doc.instruments[instr_idx];

    release_assert(maybe_instr);
    auto keysplit = maybe_instr->keysplit;  // Make a copy

    release_assert(keysplit.size() <= MAX_KEYSPLITS);
    if (keysplit.size() >= MAX_KEYSPLITS) {
        return nullptr;
    }

    Chromatic min_note = 0;
    if (keysplit.size()) {
        min_note = keysplit.back().min_note;
    }

    release_assert(patch_idx <= keysplit.size());
    keysplit.insert(keysplit.begin() + (ptrdiff_t) patch_idx, InstrumentPatch{
        .min_note = min_note,
    });

    return make_command(SetKeysplit((InstrumentIndex) instr_idx, std::move(keysplit)));
}

MaybeEditBox try_remove_patch(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx
) {
    release_assert(instr_idx < doc.instruments.size());
    auto & maybe_instr = doc.instruments[instr_idx];

    release_assert(maybe_instr);
    auto keysplit = maybe_instr->keysplit;  // Make a copy

    if (keysplit.size() <= 1) {
        return nullptr;
    }

    release_assert(patch_idx < keysplit.size());
    keysplit.erase(keysplit.begin() + (ptrdiff_t) patch_idx);

    return make_command(SetKeysplit((InstrumentIndex) instr_idx, std::move(keysplit)));
}

MaybeEditBox try_move_patch_down(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx
) {
    release_assert(instr_idx < doc.instruments.size());
    auto & maybe_instr = doc.instruments[instr_idx];

    release_assert(maybe_instr);
    auto keysplit = maybe_instr->keysplit;  // Make a copy

    // Guard against integer overflow even though MAX should never be passed in.
    if (patch_idx >= keysplit.size() || patch_idx + 1 >= keysplit.size()) {
        return nullptr;
    }

    std::swap(keysplit[patch_idx], keysplit[patch_idx + 1]);

    return make_command(SetKeysplit((InstrumentIndex) instr_idx, std::move(keysplit)));
}

MaybeEditBox try_move_patch_up(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx
) {
    if (patch_idx == 0) {
        return nullptr;
    }
    return try_move_patch_down(doc, instr_idx, patch_idx - 1);
}

std::tuple<EditBox, size_t> set_min_key(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, doc::Chromatic value
) {
    release_assert(instr_idx < doc.instruments.size());
    auto & maybe_instr = doc.instruments[instr_idx];

    release_assert(maybe_instr);
    auto keysplit = maybe_instr->keysplit;  // Make a copy

    size_t npatch = keysplit.size();
    release_assert(patch_idx < npatch);
    keysplit[patch_idx].min_note = value;

    auto get_note = [&keysplit](size_t patch_idx) -> Chromatic {
        return keysplit[patch_idx].min_note;
    };

    // Bubble sort the patch into position.
    // This will probably behave oddly if patches other than patch_idx are out of order.
    // But I don't care too much. TODO add a "sort patches" button?
    while (patch_idx >= 1 && !(get_note(patch_idx - 1) <= get_note(patch_idx))) {
        std::swap(keysplit[patch_idx - 1], keysplit[patch_idx]);
        patch_idx--;
    }
    while (patch_idx + 1 < npatch && !(get_note(patch_idx) <= get_note(patch_idx + 1))) {
        std::swap(keysplit[patch_idx], keysplit[patch_idx + 1]);
        patch_idx++;
    }

    return {
        make_command(SetKeysplit(
            (InstrumentIndex) instr_idx, std::move(keysplit), true
        )),
        patch_idx,
    };
}

// Single-patch edits. All replace the entire patch,
// and merge with other edits of the same instrument and patch index.

static InstrumentPatch const& get_patch(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx
) {
    release_assert(instr_idx < doc.instruments.size());
    auto & maybe_instr = doc.instruments[instr_idx];
    release_assert(maybe_instr);

    auto & keysplit = maybe_instr->keysplit;
    release_assert(patch_idx < keysplit.size());
    return keysplit[patch_idx];
}

static InstrumentPatch & get_patch_mut(
    doc::Document & doc, size_t instr_idx, size_t patch_idx
) {
    return const_cast<InstrumentPatch &>(get_patch(doc, instr_idx, patch_idx));
}

/// It's only safe to merge multiple edits if they edit the same location,
/// meaning that undoing the first edit produces the same document
/// whether the second edit was undone or not.
struct EditLocation {
    InstrumentIndex instr;
    uint8_t patch;

    DEFAULT_EQUALABLE(EditLocation)
};

class PatchSetter {
    EditLocation _path;
    InstrumentPatch _value;

public:
    PatchSetter(size_t instr_idx, size_t patch_idx, InstrumentPatch value) {
        release_assert(instr_idx < MAX_INSTRUMENTS);
        release_assert(patch_idx < MAX_KEYSPLITS);

        _path.instr = (InstrumentIndex) instr_idx;
        _path.patch = (uint8_t) patch_idx;
        _value = value;
    }

    static constexpr ModifiedFlags _modified = ModifiedFlags::InstrumentsEdited;

    void apply_swap(doc::Document & doc) {
        auto & patch = get_patch_mut(doc, _path.instr, _path.patch);
        std::swap(patch, _value);
    }

    using Impl = ImplEditCommand<PatchSetter, Override::CanMerge>;
    bool can_merge(BaseEditCommand & prev) const {
        if (auto p = typeid_cast<Impl *>(&prev)) {
            return p->_path == _path;
        }
        return false;
    }
};

EditBox set_sample_idx(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, doc::SampleIndex value
) {
    auto patch = get_patch(doc, instr_idx, patch_idx);
    patch.sample_idx = value;
    return make_command(PatchSetter(instr_idx, patch_idx, patch));
}

EditBox set_attack(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, uint8_t value
) {
    auto patch = get_patch(doc, instr_idx, patch_idx);
    patch.adsr.attack_rate = value;
    return make_command(PatchSetter(instr_idx, patch_idx, patch));
}

EditBox set_decay(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, uint8_t value
) {
    auto patch = get_patch(doc, instr_idx, patch_idx);
    patch.adsr.decay_rate = value;
    return make_command(PatchSetter(instr_idx, patch_idx, patch));
}

EditBox set_sustain(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, uint8_t value
) {
    auto patch = get_patch(doc, instr_idx, patch_idx);
    patch.adsr.sustain_level = value;
    return make_command(PatchSetter(instr_idx, patch_idx, patch));
}

EditBox set_decay2(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, uint8_t value
) {
    auto patch = get_patch(doc, instr_idx, patch_idx);
    patch.adsr.decay_2 = value;
    return make_command(PatchSetter(instr_idx, patch_idx, patch));
}

}  // namespace
