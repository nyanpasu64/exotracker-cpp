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
///
/// All mutations occurring in History must be sent over to the audio thread
/// to keep it in sync.
/// Currently we use an unbounded linked-list queue from main to audio thread.
/// But if we switch to a ring buffer, the audio thread may reject mutations.
/// I want History to be compatible with such an API.
/// So History::push() must be called after pushing to the audio thread,
/// and History::undo() must return an "undo command" without applying it.
///
/// This complicates the API. Is it worth it? I don't know.
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
    /// To modify the document, use EditBox/CursorEdit.
    doc::Document const & get_document() const {
        return _document;
    }

    /// Clears redo stack, mutates document, pushes command into undo history.
    void push(CursorEdit command);

    /// If undo stack non-empty, returns the command that will be undone,
    /// used to update GUI cursor location and send to the audio thread.
    MaybeCursorEdit get_undo() const;

    /// If undo stack non-empty, applies and moves command from undo to redo stack.
    void undo();

    /// If redo stack non-empty, returns the command that will be redone,
    /// used to update GUI cursor location and send to the audio thread.
    MaybeCursorEdit get_redo() const;

    /// If redo stack non-empty, applies and moves command from redo to undo stack.
    /// and returns a copy of the command, used to update GUI cursor location
    /// and send to the audio thread.
    void redo();
};

}
