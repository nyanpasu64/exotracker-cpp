#include "instr_warnings.h"
#include "gui/lib/format.h"

namespace gui::lib::instr_warnings {

using gui::lib::format::format_hex_2;

KeysplitWarningIter::KeysplitWarningIter(
    doc::Document const& doc, doc::Instrument const& instr
)
    : _doc(doc)
    , _instr(instr)
    , _patch_idx(0)
    , _curr_min_note(-1)
{}

std::optional<PatchWarnings> KeysplitWarningIter::next() {
    auto const& keysplit = _instr.keysplit;
    if (_patch_idx >= keysplit.size()) {
        return {};
    }
    auto const& patch = keysplit[_patch_idx];

    auto const& samples = _doc.samples;

    std::vector<QString> warnings;

    if (!samples[patch.sample_idx].has_value()) {
        warnings.push_back(
            QObject::tr("Sample %1 not found; keysplit will not play")
                .arg(format_hex_2(patch.sample_idx))
        );
    }
    if ((int) patch.min_note <= _curr_min_note) {
        warnings.push_back(
            QObject::tr("Min key %1 out of order; keysplit will not play")
                .arg(patch.min_note)
        );
    } else {
        _curr_min_note = patch.min_note;
    }

    auto const curr_patch_idx = _patch_idx;
    _patch_idx++;
    return PatchWarnings {
        curr_patch_idx,
        std::move(warnings),
    };
}


}
