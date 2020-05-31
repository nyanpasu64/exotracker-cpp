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
#include <memory>
#include <vector>
#include <functional>


namespace gui {
namespace pattern_editor {

class PatternEditorPanel : public QWidget
{
    W_OBJECT(PatternEditorPanel)
public:
    explicit PatternEditorPanel(QWidget *parent);

signals:

public slots:

public:
    // Upon construction, history = dummy_history, until a document is created and assigned.
    history::History _dummy_history;

    /// Stores document and undo/redo history.
    /// Is read by PatternEditorPanel running in main thread.
    /// When switching documents, can be reassigned by MainWindow(?) running in main thread.
    std::reference_wrapper<history::History> _history;

    doc::BeatFraction _row_duration_beats = {1, 4};
    bool _is_zoomed = false;

    // Visual state.
    std::unique_ptr<QPixmap> _pixmap;

    QFont _stepFont, _headerFont;
    int _stepFontWidth, _stepFontHeight, _stepFontAscend, _stepFontLeading;

    // screenPos = pos - viewportPos.
    // pos = viewportPos + screenPos.
    QPoint _viewportPos;

    int _dxWidth = 64;
    int _dyHeightPerRow = 16;

    int _widthSpace, _widthSpaceDbl;
    int _stepNumWidth;
    int _baseTrackWidth;
    int _toneNameWidth, _instWidth;
    int _volWidth;
    int _effWidth, _effIDWidth, _effValWidth;

    // impl

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
