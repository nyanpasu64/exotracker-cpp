#pragma once

#include "doc.h"
#include "edit_common.h"
#include "util/copy_move.h"

#include <boost/core/noncopyable.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace gui::history {

using edit::CursorEdit;
using edit::MaybeCursorEdit;

struct Success {
    bool succeeded;
};

/// Class is not thread-safe, and is only called from GUI thread.
class History {
private:
    doc::Document _document;
    std::vector<CursorEdit> undo_stack;
    std::vector<CursorEdit> redo_stack;

public:
    History(doc::Document initial_state);
    DISABLE_COPY(History)
    DEFAULT_MOVE(History)

    /// Get a const reference to the document.
    /// To modify the document, use Command.
    doc::Document const & get_document() const {
        return _document;
    }

    /// Clears redo stack, mutates document, pushes command into undo history.
    void push(CursorEdit command);

    /// If undo stack non-empty, moves command from undo to redo stack
    /// and returns a copy of the command, used to update GUI cursor location
    /// and send to the audio thread.
    MaybeCursorEdit undo();

    /// If redo stack non-empty, moves command from redo to undo stack
    /// and returns a copy of the command, used to update GUI cursor location
    /// and send to the audio thread.
    MaybeCursorEdit redo();
};

}
