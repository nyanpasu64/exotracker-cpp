#ifndef PATTERNEDITORPANEL_H
#define PATTERNEDITORPANEL_H

#include "doc.h"
#include "gui/history.h"

#include <verdigris/wobjectdefs.h>

#include <QWidget>
#include <QPixmap>
#include <QFont>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QHoverEvent>
#include <QWheelEvent>
#include <QEvent>
#include <QRect>
#include <QColor>
#include <QUndoStack>
#include <QString>
#include <QPoint>

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

// This is undefined behavior. I don't care.
#ifndef PatternEditorPanel_INTERNAL
#define PatternEditorPanel_INTERNAL private
#endif

class PatternEditorPanel : public QWidget
{
    W_OBJECT(PatternEditorPanel)
public:
    explicit PatternEditorPanel(QWidget *parent);

signals:

public slots:

PatternEditorPanel_INTERNAL:
    // Upon construction, history = dummy_history, until a document is created and assigned.
    history::History _dummy_history;

    /// Stores document and undo/redo history.
    /// Is read by PatternEditorPanel running in main thread.
    /// When switching documents, can be reassigned by MainWindow(?) running in main thread.
    std::reference_wrapper<history::History> _history;

    // Editing state, set by user interactions.
    ColumnCollapse column_collapse = ColumnCollapse::Full;
    doc::BeatFraction _row_duration_beats = {1, 4};
    bool _is_zoomed = false;

    // screen_pos = pos - viewport_pos.
    // pos = viewport_pos + screen_pos.
    QPoint _viewport_pos = {0, 0};

    // Private state, set by changing settings.
    QFont _header_font;
    QFont _pattern_font;

    // Cached private state. Be sure to update when changing fonts.
    PatternFontMetrics _pattern_font_metrics;
    int _dy_height_per_row;

    // Cached image.
    QImage _image;
    QImage _temp_image;

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
