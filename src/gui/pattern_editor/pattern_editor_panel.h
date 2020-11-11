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

using doc::GridIndex;
using RowIndex = uint32_t;
using doc::BeatFraction;
using timing::GridAndBeat;

enum class StepDirection {
    Down,
    RightDigits,
    RightEffect,
    Right,
    COUNT
};

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
    X(top) SEP\
    X(bottom) SEP\
    X(prev_pattern) SEP \
    X(next_pattern) SEP \
    X(left) SEP \
    X(right) SEP \
    X(scroll_left) SEP \
    X(scroll_right)

//    X(prev_channel) SEP
//    X(next_channel)

#define SHORTCUTS(X, SEP) \
    X(escape) SEP\
    X(toggle_edit) SEP\
    X(delete_key) SEP\
    X(note_cut) SEP\
    X(select_all) SEP\
    X(selection_padding)

struct PatternEditorShortcuts {
    // [0] is just the keystroke, [1] is with Shift pressed.
    #define X(KEY) \
        ShortcutPair KEY;
    SHORTCUT_PAIRS(X, )
    #undef X

    #define X(KEY)  QShortcut KEY;
    SHORTCUTS(X, )
    #undef X

    explicit PatternEditorShortcuts(QWidget * widget);
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
using history::GetDocument;

class PatternEditorPanel : public QWidget
{
    W_OBJECT(PatternEditorPanel)
public:
    explicit PatternEditorPanel(MainWindow * win, QWidget * parent = nullptr);

PatternEditorPanel_INTERNAL:
    using Super = QWidget;

    // # Non-user-facing state.

    /// Parent pointer of the subclass type.
    MainWindow & _win;

    /// Stores document and undo/redo history.
    /// Is read by PatternEditorPanel running in main thread.
    /// When switching documents, can be reassigned by MainWindow(?) running in main thread.
    GetDocument _get_document;

    // Cached private state. Be sure to update when changing fonts.
    PatternFontMetrics _pattern_font_metrics;

    // TODO mark as uint32_t if possible cleanly
    int _pixels_per_row;

    // Cached image.
    QImage _image;  // TODO remove?
    QImage _temp_image;

    // # User interaction internals.
    PatternEditorShortcuts _shortcuts;
    bool _edit_mode = false;

    // # Editing state, set by user interactions.
    int _zoom_level = 4;
    int _octave = 5;
    int _step = 1;  // can't remember if it will be saved on close, or defaulted via settings dialog
    StepDirection _step_direction = StepDirection::Down;
    bool _step_to_event = false;
    // TODO add speed-1 zoom

    // Non-empty if free scrolling is enabled.
    std::optional<GridAndBeat> _free_scroll_position;

// Interface
public:
    /// Called by main function.
    void set_history(GetDocument get_document) {
        _get_document = get_document;
    }

    // Trying to paint a PatternEditorPanel with an empty history results in a crash,
    // so setting an empty history is useless.
    // void unset_history();

    #define PROPERTY(TYPE, FIELD, METHOD) \
        [[nodiscard]] TYPE METHOD() const { \
            return FIELD; \
        } \
        void set_##METHOD(TYPE v) { \
            FIELD = v; \
            update(); \
        }

    PROPERTY(int, _zoom_level, zoom_level)
    PROPERTY(int, _octave, octave)
    PROPERTY(int, _step, step)
    PROPERTY(StepDirection, _step_direction, step_direction)
    void set_step_direction_int(int v) {
        // QComboBox::currentIndexChanged is bound to this method,
        // but passes in int (not enum).
        set_step_direction(StepDirection(v));
    }
    PROPERTY(bool, _step_to_event, step_to_event)

// Implementation
PatternEditorPanel_INTERNAL:
    [[nodiscard]] doc::Document const& get_document() const;

    void resizeEvent(QResizeEvent* event) override;

    // paintEvent() is a pure function (except for screen output).
    void paintEvent(QPaintEvent *event) override;

    // QShortcut signals are bound to a lambda slot, which calls these methods.

    #define X(KEY) \
        void KEY##_pressed();
    SHORTCUT_PAIRS(X, )
    SHORTCUTS(X, )
    #undef X

    void keyPressEvent(QKeyEvent * event) override;
    void keyReleaseEvent(QKeyEvent * event) override;
};

#endif // PATTERNEDITORPANEL_H

// namespace
}
