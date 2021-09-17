#pragma once

#include "doc.h"

#include <QString>

#include <vector>
#include <optional>

namespace gui::lib::instr_warnings {

struct PatchWarnings {
    size_t patch_idx;
    std::vector<QString> warnings;
};

class KeysplitWarningIter {
    doc::Document const& _doc;
    doc::Instrument const& _instr;
    size_t _patch_idx;
    int _curr_min_note;

public:
    KeysplitWarningIter(doc::Document const& doc, doc::Instrument const& instr);

    std::optional<PatchWarnings> next();
};

}
