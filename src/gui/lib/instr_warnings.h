#pragma once

#include "doc.h"
#include "gui/lib/format.h"
#include "util/defer.h"

#include <gsl/span>

#include <QString>

#include <vector>
#include <optional>

namespace gui::lib::instr_warnings {

using namespace doc;
using gui::lib::format::format_hex_2;

struct PatchWarnings {
    /// If -1, no items left.
    ptrdiff_t patch_idx;

    std::vector<QString> warnings;
};

class KeysplitWarningIter {
    doc::Document const& _doc;
    doc::Instrument const& _instr;
    size_t _patch_idx = 0;
    int _curr_min_note = -1;

public:
    KeysplitWarningIter(doc::Document const& doc, doc::Instrument const& instr)
        : _doc(doc)
        , _instr(instr)
    {}

    PatchWarnings next() {
        auto const& keysplit = _instr.keysplit;
        if (_patch_idx >= keysplit.size()) {
            return {-1, {}};
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

        defer { _patch_idx++; };
        return PatchWarnings {
            (ptrdiff_t) _patch_idx,
            std::move(warnings),
        };
    }
};

}
