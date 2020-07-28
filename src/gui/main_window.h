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

/// Some pattern cells (like instruments, volumes, and effects)
/// have multiple characters entered in sequence.
/// This stores the typing progress within those cells.
class CursorAndDigit {
    Cursor _cursor;
    int _digit = 0;

public:
    [[nodiscard]] Cursor const& get() const;
    Cursor const& operator*() const;
    Cursor const* operator->() const;

    [[nodiscard]] Cursor & get_mut();
    void set(Cursor cursor);

    [[nodiscard]] int digit_index() const;
    int advance_digit();
    void reset_digit();
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

    CursorAndDigit _cursor;
    std::optional<Cursor> _select_begin;

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
