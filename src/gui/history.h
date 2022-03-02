#pragma once

#include "doc.h"
#include "edit_common.h"
#include "gui/cursor.h"
#include "util/copy_move.h"

#include <optional>
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
    std::vector<UndoFrame> _undo_stack;
    std::vector<UndoFrame> _redo_stack;

    /// If true, you can merge edits into the most recent undo step (if applicable).
    /// Set to true upon pushing edits, set to false upon undo, and should already be
    /// false when a redo succeeds.
    bool _newly_pushed = false;

    bool _dirty = false;

public:
    History(doc::Document initial_state);
    DISABLE_COPY(History)
    DEFAULT_MOVE(History)

    /// Get a const reference to the document.
    /// To modify the document, use EditBox/CursorEdit.
    doc::Document const& get_document() const noexcept {
        return _document;
    }

    bool is_dirty() const {
        return _dirty;
    }
    void mark_saved() {
        // TODO track *which* undo state is clean,
        // instead of only clearing dirty flag when saving.
        // https://gitlab.com/exotracker/exotracker-cpp/-/issues/111
        _dirty = false;
    }

    /// Clears redo stack, mutates document, pushes command into undo history.
    void push(UndoFrame command);

    /// Returns whether the undo stack is non-empty.
    bool can_undo() const;
    /// Returns whether the redo stack is non-empty.
    bool can_redo() const;

    /*
    Currently we use an unbounded linked-list queue from main to audio thread.
    But if we switch to a bounded queue (ring buffer), the audio thread may reject
    messages. In that case, check that the queue isn't full before calling undo()
    and sending the returned command over the queue.
    */
    /// If the undo stack is empty, does nothing and returns nullopt. Otherwise,
    /// applies the command on top of the undo stack and moves it to the redo stack,
    /// and returns a clone of the command (which gets sent to the audio thread)
    /// and the new GUI cursor location (or nullopt).
    MaybeCursorEdit try_undo();

    /// If the redo stack is empty, does nothing and returns nullopt. Otherwise,
    /// applies the command on top of the redo stack and moves it to the undo stack,
    /// and returns a clone of the command (which gets sent to the audio thread)
    /// and the new GUI cursor location (or nullopt).
    MaybeCursorEdit try_redo();
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
