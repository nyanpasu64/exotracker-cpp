#include "edit_instr.h"
#include "edit_impl.h"
#include "edit/modified.h"
#include "util/release_assert.h"
#include "util/compare.h"
#include "util/typeid_cast.h"

#include <utility>  // std::declval

namespace edit::edit_instr {

using namespace doc;
using edit_impl::make_command;

// Keysplit edits which add, remove, or reorder patches.
// They do not coalesce with other operations.

// could alternatively insert or remove a single patch.
// but that requires reserving MAX_KEYSPLITS (128) items in each instrument's keysplit
// in the audio thread, both when sending over a document
// and when inserting instruments later on.
// this eats RAM at rest, and is easy to get wrong.
//
// instead this replaces the entire keysplit on each edit.
// technically it wastes ram in the undo history
// if you add 128 keysplits and then remove/add more,
// but this already happens if you add hundreds/thousands of events in a pattern
// and then remove/add more (which is more likely than a huge keysplit).
struct SetKeysplit {
    InstrumentIndex _instr_idx;
    std::vector<InstrumentPatch> _keysplit;

// impl
    /// If the command holds a row to be inserted, reserve memory for each cell.
    /// Once a cell gets swapped into the document,
    /// adding blocks must not allocate memory, to prevent blocking the audio thread.
    SetKeysplit(InstrumentIndex instr_idx, std::vector<InstrumentPatch> keysplit)
        : _instr_idx(instr_idx)
        , _keysplit(std::move(keysplit))
    {}

    void apply_swap(doc::Document & doc) {
        auto & maybe_instr = doc.instruments[_instr_idx];
        release_assert(maybe_instr);
        std::swap(maybe_instr->keysplit, _keysplit);
    }

    bool can_coalesce(BaseEditCommand &) const {
        return false;
    }

    static constexpr ModifiedFlags _modified = ModifiedFlags::InstrumentsEdited;
};

MaybeEditBox try_add_patch(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx
) {
    release_assert(instr_idx < doc.instruments.v.size());
    auto & maybe_instr = doc.instruments[instr_idx];

    release_assert(maybe_instr);
    // copy
    auto keysplit = maybe_instr->keysplit;
    release_assert(keysplit.size() <= MAX_KEYSPLITS);
    if (keysplit.size() >= MAX_KEYSPLITS) {
        return nullptr;
    }

    release_assert(patch_idx <= keysplit.size());
    keysplit.insert(keysplit.begin() + (ptrdiff_t) patch_idx, InstrumentPatch{});

    return make_command(SetKeysplit((InstrumentIndex) instr_idx, std::move(keysplit)));
}

MaybeEditBox try_remove_patch(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx
) {
    release_assert(instr_idx < doc.instruments.v.size());
    auto & maybe_instr = doc.instruments[instr_idx];

    release_assert(maybe_instr);
    // copy
    auto keysplit = maybe_instr->keysplit;
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
    release_assert(instr_idx < doc.instruments.v.size());
    auto & maybe_instr = doc.instruments[instr_idx];

    release_assert(maybe_instr);
    // copy
    auto keysplit = maybe_instr->keysplit;

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

// Single-patch edits. All replace the entire patch,
// and coalesce with other edits of the same instrument and patch index.

static InstrumentPatch const& get_patch(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx
) {
    release_assert(instr_idx < doc.instruments.v.size());
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

/// It's only safe to coalesce multiple edits if they edit the same location,
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

    bool can_coalesce(BaseEditCommand & prev_edit_command) const {
        using SelfEditCommand = edit_impl::ImplEditCommand<PatchSetter>;

        if (auto prev = typeid_cast<SelfEditCommand *>(&prev_edit_command)) {
            return prev->_path == _path;
        }
        return false;
    }
};

#define FIELD(PATH) \
    decltype(std::declval<doc::InstrumentPatch>().PATH), \
    offsetof(doc::InstrumentPatch, PATH)

EditBox edit_min_key(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, doc::Chromatic value
) {
    auto patch = get_patch(doc, instr_idx, patch_idx);
    patch.min_note = value;
    return make_command(PatchSetter(instr_idx, patch_idx, patch));
}

EditBox edit_sample_idx(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, doc::SampleIndex value
) {
    auto patch = get_patch(doc, instr_idx, patch_idx);
    patch.sample_idx = value;
    return make_command(PatchSetter(instr_idx, patch_idx, patch));
}

EditBox edit_attack(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, uint8_t value
) {
    auto patch = get_patch(doc, instr_idx, patch_idx);
    patch.adsr.attack = value;
    return make_command(PatchSetter(instr_idx, patch_idx, patch));
}

EditBox edit_decay(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, uint8_t value
) {
    auto patch = get_patch(doc, instr_idx, patch_idx);
    patch.adsr.decay = value;
    return make_command(PatchSetter(instr_idx, patch_idx, patch));
}

EditBox edit_sustain(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, uint8_t value
) {
    auto patch = get_patch(doc, instr_idx, patch_idx);
    patch.adsr.sustain = value;
    return make_command(PatchSetter(instr_idx, patch_idx, patch));
}

EditBox edit_decay2(
    doc::Document const& doc, size_t instr_idx, size_t patch_idx, uint8_t value
) {
    auto patch = get_patch(doc, instr_idx, patch_idx);
    patch.adsr.release = value;
    return make_command(PatchSetter(instr_idx, patch_idx, patch));
}

}  // namespace
