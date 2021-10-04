#pragma once
// Do *not* include any other widgets in this header and create an include cycle.
// Other widgets include main_window.h, since they rely on MainWindow for data/signals.
#include "history.h"
#include "cursor.h"
#include "doc.h"
#include "edit_common.h"
#include "timing_common.h"
#include "audio/output.h"
#include "util/copy_move.h"
#include "util/release_assert.h"

#include <gsl/span>
#include <verdigris/wobjectdefs.h>

#include <QMainWindow>
#include <QWidget>

#include <functional>  // std::function
#include <memory>
#include <optional>
#include <variant>

namespace gui::instr_dialog {
    class InstrumentDialog;
}
namespace gui::sample_dialog {
    class SampleDialog;
}

namespace gui::main_window {
#ifndef main_window_INTERNAL
#define main_window_INTERNAL private
#endif

using audio::output::AudioThreadHandle;
using timing::GridAndBeat;
using timing::MaybeSequencerTime;

enum class AudioState {
    Stopped,
    Starting,
    PlayHasStarted,
};

using cursor::Cursor;
using cursor::CursorX;

/// External representation of a selection in the document,
/// used for rendering and copy/paste.
/// Stores top and bottom, and left and right.
///
/// left and right are both inclusive, and must lie in bounds.
///
/// top is inclusive, but bottom is exclusive.
///
/// top.grid must lie in-bounds, and top.beat generally remains in-bounds.
/// bottom.grid must lie in-bounds,
/// but bottom.beat can equal the grid cell length.
struct Selection {
    CursorX left;
    CursorX right;
    GridAndBeat top;
    GridAndBeat bottom;
};

enum class SelectionMode {
    Normal,
    SelectChannels,
    SelectAll,
};

using ColumnToNumSubcol = gsl::span<cursor::SubColumnIndex>;

/// Internal representation of selection, used for cursor movement.
/// Stores the begin position (usually fixed) and endpoint (usually moves with cursor),
/// and how much extra time below the selection's bottom (either begin or end) to select.
class RawSelection {
    /// Starting point of the selection.
    Cursor _begin;

    /// Endpoint of the selection. Always updated when the cursor moves,
    /// but select-all can move selection without moving cursor.
    Cursor _end;

    SelectionMode _mode = SelectionMode::Normal;
    cursor::ColumnIndex _orig_left = cursor::ColumnIndex(-1);
    cursor::ColumnIndex _orig_right = cursor::ColumnIndex(-1);

    /// How many beats to select below the bottom endpoint
    /// (whichever of _begin and cursor is lower).
    doc::BeatFraction _bottom_padding;

    // impl
public:
    explicit RawSelection(Cursor cursor, int rows_per_beat);

    Selection get_select() const;

    doc::BeatFraction bottom_padding() const {
        return _bottom_padding;
    }

    void set_end(Cursor end);

    void toggle_padding(int rows_per_beat);

    void select_all(
        doc::Document const& document,
        ColumnToNumSubcol col_to_nsubcol,
        int rows_per_beat
    );
};

/// Stores cursor, selection,
/// and how many beats to select below the bottom endpoint.
///
/// Selections are a hard problem. Requirements which led to this API design at
/// https://docs.google.com/document/d/1HBrF1W_5vKFMwHbaN6ONvtnmGAgawlJYsdZTbTUClmA/edit#heading=h.q2iq7gfnt5i8
class CursorAndSelection {
private:
    Cursor _cursor{};
    std::optional<RawSelection> _select{};

    // impl
public:
    // # Cursor position
    Cursor const& get() const;
    Cursor const& operator*() const;
    Cursor const* operator->() const;

    /// Only called by PatternEditorImpl during edit/undo/redo.
    Cursor & get_mut();

    /// Moving the cursor always updates the selection endpoint.
    void set(Cursor cursor);
    void set_x(CursorX x);
    void set_y(GridAndBeat y);


    // # Selection
    std::optional<RawSelection> raw_select() const;
    std::optional<RawSelection> & raw_select_mut();

    std::optional<Selection> get_select() const;

    /// If selection not enabled, begin from cursor position.
    /// Otherwise continue selection.
    void enable_select(int rows_per_beat);

    /// Clear selection.
    void clear_select();
};

using CursorOrHere = std::optional<Cursor>;

namespace MoveCursor_ {
    struct IgnoreCursor {};
    struct MoveFrom {
        CursorOrHere before_or_here;
        CursorOrHere after_or_here;
    };

    using MoveCursor = std::variant<IgnoreCursor, MoveFrom>;

    inline constexpr MoveCursor IGNORE_CURSOR = IgnoreCursor{};
}

using MoveCursor_::MoveCursor;

inline MoveCursor move_to(Cursor cursor) {
    return MoveCursor_::MoveFrom{{}, cursor};
}

inline MoveCursor move_to_here() {
    return MoveCursor_::MoveFrom{{}, {}};
}

using gui::history::History;
using gui::history::GetDocument;

class StateComponent {
private:
    History _history;

    // If we don't use QListView, we really don't need a "cursor_moved" signal.
    // Each method updates the cursor location, then the screen is redrawn at 60fps.
    CursorAndSelection _cursor{};

    int _instrument = 0;

public:
    bool _insert_instrument = true;  // no side effects when changed, so let the world see

    /// Whether the GUI is being updated in response to events.
    bool _during_update = false;

// impl
public:
    StateComponent(doc::Document document)
        : _history(std::move(document))
    {}

    History const& history() const {
        return _history;
    }

    GetDocument document_getter() {
        return GetDocument(_history);
    }

    doc::Document const& document() const {
        return _history.get_document();
    }

    Cursor const& cursor() const {
        return _cursor.get();
    }

    std::optional<Selection> select() const {
        return _cursor.get_select();
    }

    std::optional<RawSelection> raw_select() const {
        return _cursor.raw_select();
    }

    doc::InstrumentIndex instrument() const {
        release_assert(size_t(_instrument) < doc::MAX_INSTRUMENTS);
        return (doc::InstrumentIndex) _instrument;
    }

    friend class StateTransaction;
};

// # GUI state mutation tracking (StateTransaction):

/// Used to find which portion of the GUI needs to be redrawn.
enum class StateUpdateFlag : uint32_t {
    None = 0,
    All = ~(uint32_t)0,
    DocumentEdited = 0x1,
    CursorMoved = 0x2,
    InstrumentSwitched = 0x4,

    DocumentReplaced = 0x100,
    InstrumentDeleted = 0x200,
};
Q_DECLARE_FLAGS(StateUpdateFlags, StateUpdateFlag)
Q_DECLARE_OPERATORS_FOR_FLAGS(StateUpdateFlags)

class MainWindow;
class MainWindowImpl;

class [[nodiscard]] StateTransaction {
private:
    MainWindowImpl * _win;

    int _uncaught_exceptions;

    /// Which part of the GUI needs to be redrawn due to events.
    StateUpdateFlags _queued_updates = StateUpdateFlag::None;
    std::optional<doc::SampleIndex> _sample_index;

// impl
private:
    /// Do not call directly; use MainWindow::edit_state() or edit_unwrap() instead.
    StateTransaction(MainWindowImpl * win);

public:
    static std::optional<StateTransaction> make(MainWindowImpl * win);

    DISABLE_COPY(StateTransaction)
    StateTransaction & operator=(StateTransaction &&) = delete;
    StateTransaction(StateTransaction && other) noexcept;

    /// Uses the destructor to update the GUI in response to changes,
    /// so does nontrivial work and could throw exceptions
    /// (no clue if exceptions propagate through Qt).
    ~StateTransaction() noexcept(false);

    StateComponent const& state() const;
private:
    StateComponent & state_mut();

// Mutations:
public:
    void update_all() {
        _queued_updates = StateUpdateFlag::All;
    }

    History const& history() const {
        return state().history();
    }
    /// Don't call directly! History::push() will not send edits to the audio thread!
    /// Instead call StateTransaction::push_edit().
    /// (Exception: AudioComponent::undo()/redo() call this as well.)
    History & history_mut();

    /// move_to() or move_to_here() saves and moves the cursor (for pattern edits).
    /// MoveCursor_::IGNORE_CURSOR doesn't move the cursor on undo/redo (for
    /// non-pattern edits).
    void push_edit(edit::EditBox command, MoveCursor cursor_move);

    /// Close the instrument dialog if open.
    void instrument_deleted();

    void set_document(doc::Document document);

    CursorAndSelection & cursor_mut();

    void set_instrument(int instrument);

    /// Only called by the sample dialog when editing the sample list.
    /// Only affects the sample dialog, not StateComponent.
    void set_sample_index(doc::SampleIndex sample);
};


/// Everything exposed to other modules goes here. GUI widgets/etc. go in MainWindowPrivate.
class MainWindow : public QMainWindow {
    W_OBJECT(MainWindow)

public:
    StateComponent _state;

    StateComponent const& state() {
        return _state;
    }

// interface
    static MainWindow & get_instance();

    /// Fails if another edit transaction is logging or responding to changes.
    ///
    /// QActions triggered by the user should never occur reentrantly,
    /// so use `unwrap(edit_state())` or `edit_unwrap()` (throws exception if concurrent).
    ///
    /// Signals generated by widgets (eg. spinboxes) may occur reentrantly
    /// if you forget to use QSignalBlocker when ~StateTransaction() sets widget values,
    /// so use `debug_unwrap(edit_state(), lambda)` (if edit in progress,
    /// assert on debug builds and otherwise skip function call)
    virtual std::optional<StateTransaction> edit_state() = 0;

    virtual StateTransaction edit_unwrap() = 0;

    virtual instr_dialog::InstrumentDialog * show_instr_dialog() = 0;

    virtual sample_dialog::SampleDialog * maybe_sample_dialog() const = 0;

    virtual sample_dialog::SampleDialog * show_sample_dialog(
        std::optional<doc::SampleIndex> sample
    ) = 0;

// constructors
    static std::unique_ptr<MainWindow> make(
        doc::Document document, QWidget * parent = nullptr
    );

    virtual ~MainWindow();

main_window_INTERNAL:
    MainWindow(doc::Document document, QWidget *parent = nullptr);
};

}
