#ifndef PATTERNEDITORPANEL_H
#define PATTERNEDITORPANEL_H

#include "doc.h"
#include "gui/history.h"

#include <verdigris/wobjectdefs.h>

#include <QWidget>
#include <QImage>
#include <QPaintEvent>

#include <cstdint>
#include <functional>  // std::reference_wrapper

namespace gui {
namespace pattern_editor {

struct PatternFontMetrics {
    /// Width of a standard character (like 'M').
    int width;

    /// Distance above the baseline of tall characters.
    /// May come from uppercase character, or font metadata.
    int ascent;
    int descent;
};

enum class ColumnCollapse {
    Full,
    HideEffects,
    NotesOnly,
};

using doc::SeqEntryIndex;
using RowIndex = uint32_t;

struct PatternAndBeat {
    SeqEntryIndex seq_entry_index = 0;
//    RowIndex row_index = 0;
    doc::BeatFraction curr_beat = 0;
};

// This is undefined behavior. I don't care.
#ifndef PatternEditorPanel_INTERNAL
#define PatternEditorPanel_INTERNAL private
#endif

// I'm starting to regret subclassing QWidget,
// which intermixes my fields with QWidget fields.
// I should've, idk, defined my own class,
// and subclassed QWidget to contain an instance of my class?

class PatternEditorPanel : public QWidget
{
    W_OBJECT(PatternEditorPanel)
public:
    explicit PatternEditorPanel(QWidget *parent);

signals:

public slots:

PatternEditorPanel_INTERNAL:
    // # Non-user-facing state.

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

    // # Editing state, set by user interactions.

    ColumnCollapse _column_collapse = ColumnCollapse::Full;
    doc::BeatFraction _beats_per_row = {1, 4};
    bool _is_zoomed = false;

    // TODO cursor_x
    PatternAndBeat _cursor_y;

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

protected:
    // overrides QWidget
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent* event) override;
};

#endif // PATTERNEDITORPANEL_H

// namespaces
}
}
