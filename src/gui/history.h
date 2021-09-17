#pragma once

#include "doc.h"
#include "edit_common.h"
#include "gui/cursor.h"
#include "util/copy_move.h"

#include <vector>

namespace gui::history {

using edit::EditBox;
using gui::cursor::Cursor;
using MaybeCursor = std::optional<Cursor>;

struct [[nodiscard]] CursorEdit {
    EditBox edit;
    MaybeCursor cursor;
};

using MaybeCursorEdit = std::optional<CursorEdit>;

struct UndoFrame {
    EditBox edit;

    MaybeCursor before_cursor;
    MaybeCursor after_cursor;
};

/// Class is not thread-safe, and is only called from GUI thread.
///
/// All mutations occurring in History must be sent over to the audio thread
/// to keep it in sync.
class History {
private:
    doc::Document _document;
    std::vector<UndoFrame> undo_stack;
    std::vector<UndoFrame> redo_stack;

public:
    History(doc::Document initial_state);
    DISABLE_COPY(History)
    DEFAULT_MOVE(History)

    /// Get a const reference to the document.
    /// To modify the document, use EditBox/CursorEdit.
    doc::Document const& get_document() const noexcept {
        return _document;
    }

    /// Clears redo stack, mutates document, pushes command into undo history.
    void push(UndoFrame command);

    /*
    Currently we use an unbounded linked-list queue from main to audio thread.
    But if we switch to a ring buffer, the audio thread may reject messages.
    I want History to be compatible with such an API.
    So History::get_undo() must return an "undo command" without applying it,
    so MainWindow can try sending it to the audio thread, and if it succeeds,
    call History::undo().

    This complicates the API. Is it worth it? I don't know.
    */
    /// If undo stack non-empty, returns the state needed to undo the GUI and audio
    /// thread: the GUI cursor location before the edit, and an EditBox to be sent to
    /// the audio thread.
    MaybeCursorEdit get_undo() const;

    /// If undo stack non-empty, applies and moves command from undo to redo stack.
    void undo();

    /// If redo stack non-empty, returns the state needed to redo the GUI and audio
    /// thread: the GUI cursor location after the edit, and an EditBox to be sent to
    /// the audio thread.
    MaybeCursorEdit get_redo() const;

    /// If redo stack non-empty, applies and moves command from redo to undo stack.
    void redo();
};

/// Cannot be used at static initialization time, before main() begins.
extern History const EMPTY_HISTORY;

/// Class wrapping a History pointer, only allowing the user to get the current document.
/// Used by GUI panel widgets to obtain the current document.
class GetDocument {
    History const* _history;

public:
    /// Holds a reference to `history` passed in.
    /// If History is owned by MainWindow
    /// and each GetDocument is owned by a child QWidget panel,
    /// then History will outlive all GetDocument pointing to it.
    GetDocument(History const& history) noexcept
        : _history(&history)
    {}

    /// Construct an empty GetDocument which returns an empty document.
    /// Cannot be called at static initialization time, before main() begins.
    static GetDocument empty() noexcept {
        return GetDocument(EMPTY_HISTORY);
    }

    doc::Document const& get_document() const noexcept {
        return _history->get_document();
    }

    doc::Document const& operator()() const noexcept {
        return _history->get_document();
    }
};

}
