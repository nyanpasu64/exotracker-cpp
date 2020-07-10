#ifndef PATTERNEDITORPANEL_H
#define PATTERNEDITORPANEL_H

#include "doc.h"
#include "gui/main_window.h"
#include "gui/history.h"
#include "timing_common.h"

#include <verdigris/wobjectdefs.h>

#include <QWidget>
#include <QImage>
#include <QPaintEvent>
#include <QShortcut>

#include <cstdint>
#include <functional>  // std::reference_wrapper

namespace gui::pattern_editor {

struct PatternFontMetrics {
    /// Width of a standard character (like 'M').
    int width;

    /// Distance above the baseline of tall characters.
    /// May come from uppercase character, or font metadata.
    int ascent;
    int descent;
};

/// Currently unused.
/// Eventually I want to make each column (like pan) independently toggleable
/// (except note is always shown).
enum class ColumnCollapse {
    Full,
    HideEffects,
    NotesOnly,
};

using doc::SeqEntryIndex;
using RowIndex = uint32_t;
using doc::BeatFraction;
using timing::PatternAndBeat;

struct ShortcutPair {
    // You can't use an array of 2 elements. Why?
    //
    // You can't list-initialize an array of non-copy-constructible types
    // with only explicit constructors.
    // `QShortcut x[2] = {widget, widget}` fails because the constructor is explicit.
    // `QShortcut x[2] = {QShortcut{widget}, QShortcut{widget}}` instead attempts to
    // copy from the initializer_list.
    //
    // So use a struct instead.

    // QShortcut's parent must not be nullptr.
    // https://code.woboq.org/qt5/qtbase/src/widgets/kernel/qshortcut.cpp.html#_ZN9QShortcutC1EP7QWidget

    QShortcut key;
    QShortcut shift_key;
};

/// This is a list of all cursor movement keys (single source of truth)
/// where you can hold Shift to create a selection.
#define SHORTCUT_PAIRS(X, SEP) \
    X(up) SEP \
    X(down) SEP \
    X(prev_beat) SEP \
    X(next_beat) SEP \
    X(prev_event) SEP \
    X(next_event) SEP \
    X(scroll_prev) SEP \
    X(scroll_next) SEP \
    X(prev_pattern) SEP \
    X(next_pattern) SEP \
    X(left) SEP \
    X(right) SEP \
    X(scroll_left) SEP \
    X(scroll_right)

//    X(prev_channel) SEP
//    X(next_channel)

#define SHORTCUTS(X, SEP) \
    X(delete_key)

struct PatternEditorShortcuts {
    // [0] is just the keystroke, [1] is with Shift pressed.
    #define X(KEY) \
        ShortcutPair KEY;
    SHORTCUT_PAIRS(X, )
    #undef X

    #define X(KEY)  QShortcut KEY;
    SHORTCUTS(X, )
    #undef X

    PatternEditorShortcuts(QWidget * widget) :
        #define COMMA ,

        #define X(PAIR) \
            PAIR{QShortcut{widget}, QShortcut{widget}}
        SHORTCUT_PAIRS(X, COMMA)
        #undef X

        #define X(KEY)  ,KEY{widget}
        SHORTCUTS(X, )
        #undef X
    {}
};

// This is undefined behavior. I don't care.
#ifndef PatternEditorPanel_INTERNAL
#define PatternEditorPanel_INTERNAL private
#endif

// I'm starting to regret subclassing QWidget,
// which intermixes my fields with QWidget fields.
// I should've, idk, defined my own class,
// and subclassed QWidget to contain an instance of my class?

using main_window::MainWindow;

class PatternEditorPanel : public QWidget
{
    W_OBJECT(PatternEditorPanel)
public:
    explicit PatternEditorPanel(MainWindow * parent);

PatternEditorPanel_INTERNAL:
    // # Non-user-facing state.

    /// Parent pointer of the subclass type.
    MainWindow & _win;

    // Upon construction, history = dummy_history, until a document is created and assigned.
    history::History _dummy_history;

    /// Stores document and undo/redo history.
    /// Is read by PatternEditorPanel running in main thread.
    /// When switching documents, can be reassigned by MainWindow(?) running in main thread.
    std::reference_wrapper<history::History> _history;

    // Cached private state. Be sure to update when changing fonts.
    PatternFontMetrics _pattern_font_metrics;

    // TODO mark as uint32_t if possible cleanly
    int _pixels_per_row;

    // Cached image.
    QImage _image;  // TODO remove?
    QImage _temp_image;

    // # User interaction internals.
    PatternEditorShortcuts _shortcuts;

    // # Editing state, set by user interactions.
    int _rows_per_beat = 4;
    bool _is_zoomed = false;

    // Non-empty if free scrolling is enabled.
    std::optional<PatternAndBeat> _free_scroll_position;

    // impl
public:
    /// Called by main function.
    void set_history(history::History & history) {
        _history = history;
    }

    /// Unsure if useful or not.
    void unset_history() {
        _history = _dummy_history;
    }

public slots:
    // update() is called by timers and user input.
    // It mutates state (cached audio playback position) and calls repaint().
    void update(timing::MaybeSequencerTime maybe_seq_time);
    W_SLOT(update, (timing::MaybeSequencerTime))

protected:

    // paintEvent() is a pure function (except for screen output).
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent* event) override;

PatternEditorPanel_INTERNAL:

    doc::Document const & get_document() {
        // Change this method if we change how history works.
        return _history.get().get_document();
    }

    // QShortcut signals are bound to a lambda slot, which calls these methods.

    #define X(KEY) \
        void KEY##_pressed();
    SHORTCUT_PAIRS(X, )
    SHORTCUTS(X, )
    #undef X
};

#endif // PATTERNEDITORPANEL_H

// namespace
}
