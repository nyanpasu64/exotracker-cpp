#ifndef MAINWINDOW_H
#define MAINWINDOW_H

// Do *not* include any other widgets in this header and create an include cycle.
// Other widgets include main_window.h, since they rely on MainWindow for data/signals.
#include "history.h"
#include "cursor.h"
#include "doc.h"
#include "edit_common.h"
#include "timing_common.h"
#include "audio/output.h"

#include <gsl/span>
#include <verdigris/wobjectdefs.h>

#include <QMainWindow>
#include <QWidget>

#include <functional>  // std::function
#include <memory>
#include <optional>

namespace gui::main_window {

using audio::output::AudioThreadHandle;
using timing::PatternAndBeat;
using timing::MaybeSequencerTime;

enum class AudioState {
    Stopped,
    Starting,
    PlayHasStarted,
};

using cursor::Cursor;
using cursor::CursorX;

/// A user selection in the document.
///
/// left and right are both inclusive, and must lie in bounds.
///
/// top is inclusive, but bottom is exclusive.
///
/// top.seq_entry_index must lie in-bounds, and top.beat generally remains in-bounds.
/// bottom.seq_entry_index must lie in-bounds,
/// but bottom.beat can equal the sequence entry length.
struct Selection {
    CursorX left;
    CursorX right;
    PatternAndBeat top;
    PatternAndBeat bottom;
};

enum class SelectionMode {
    Normal,
    SelectChannels,
    SelectAll,
};

using ColumnToNumSubcol = gsl::span<cursor::SubColumnIndex>;

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

    [[nodiscard]] Selection get_select() const;

    [[nodiscard]] doc::BeatFraction bottom_padding() const {
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
/// Some pattern cells (like instruments, volumes, and effects)
/// have multiple characters entered in sequence.
/// digit_index() stores the typing progress within those cells.
///
/// Selections are a hard problem. Requirements which led to this API design at
/// https://docs.google.com/document/d/1HBrF1W_5vKFMwHbaN6ONvtnmGAgawlJYsdZTbTUClmA/edit#heading=h.q2iq7gfnt5i8
class CursorAndSelection : public QObject {
    W_OBJECT(CursorAndSelection)

private:
    Cursor _cursor;
    int _digit = 0;
    std::optional<RawSelection> _select{};

    // impl
public:
    // # Cursor position
    [[nodiscard]] Cursor const& get() const;
    Cursor const& operator*() const;
    Cursor const* operator->() const;

    void cursor_moved() W_SIGNAL(cursor_moved)

    /// Moving the cursor always updates the selection endpoint.
    void set(Cursor cursor);
    void set_x(CursorX x);
    void set_y(PatternAndBeat y);

    // # Digits within a cell
    [[nodiscard]] int digit_index() const;
    int advance_digit();
    void reset_digit();

    // # Selection
    [[nodiscard]] std::optional<RawSelection> raw_select() const;
    [[nodiscard]] std::optional<RawSelection> & raw_select_mut();
    [[nodiscard]] std::optional<Selection> get_select() const;

    /// If selection not enabled, begin from cursor position.
    /// Otherwise continue selection.
    void enable_select(int rows_per_beat);

    /// Clear selection.
    void clear_select();
};

/// Everything exposed to other modules goes here. GUI widgets/etc. go in MainWindowPrivate.
class MainWindow : public QMainWindow
{
    W_OBJECT(MainWindow)

public:
    // Just make it a grab bag of fields for now.
    // We really don't need a "cursor_moved" signal.
    // Each method updates the cursor location, then the screen is redrawn at 60fps.

    // TODO encapsulate selection begin (x, y) and cursor (x, y)
    // into a Selection class.
    // set_cursor() and select_to()?
    // set_cursor(bool select)?

    CursorAndSelection _cursor;

    int _instrument = 0;
    bool _insert_instrument = true;

public:
    // impl
    static std::unique_ptr<MainWindow> make(
        doc::Document document, QWidget * parent = nullptr
    );

    static MainWindow & get_instance();

    MainWindow(QWidget *parent = nullptr);
    virtual void _() = 0;
    virtual ~MainWindow();

    virtual std::optional<AudioThreadHandle> const & audio_handle() const = 0;

    virtual AudioState audio_state() const = 0;

    /// If maybe_cursor is present, assigns it to _cursor.
    /// If advance_digit is true, calls _cursor.advance_digit(),
    /// otherwise reset_digit().
    /// push_edit() saves (command, old_cursor, _cursor) into the undo history,
    /// and resets _cursor.digit_index() unless preserve_digit is true.
    virtual void push_edit(
        edit::EditBox command,
        std::optional<Cursor> maybe_cursor,
        bool advance_digit = false
    ) = 0;

signals:
    void gui_refresh(MaybeSequencerTime maybe_seq_time)
        W_SIGNAL(gui_refresh, (MaybeSequencerTime), maybe_seq_time)

public slots:
    virtual void restart_audio_thread() = 0;
    W_SLOT(restart_audio_thread)
};


}

#endif // MAINWINDOW_H
