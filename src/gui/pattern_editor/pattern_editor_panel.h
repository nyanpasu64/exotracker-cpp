#ifndef PATTERNEDITORPANEL_H
#define PATTERNEDITORPANEL_H

#include "document.h"
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
    history::History dummy_history;

    /// Stores document and undo/redo history.
    /// Is read by PatternEditorPanel running in main thread.
    /// When switching documents, can be reassigned by MainWindow(?) running in main thread.
    std::reference_wrapper<history::History> history;

    doc::BeatFraction row_duration_beats = {1, 4};
    bool is_zoomed = false;

    // Visual state.
    std::unique_ptr<QPixmap> pixmap_;

    QFont stepFont_, headerFont_;
    int stepFontWidth_, stepFontHeight_, stepFontAscend_, stepFontLeading_;

    // screenPos = pos - viewportPos.
    // pos = viewportPos + screenPos.
    QPoint viewportPos;

    int dxWidth = 64;
    int dyHeightPerRow = 16;

    int widthSpace_, widthSpaceDbl_;
    int stepNumWidth_;
    int baseTrackWidth_;
    int toneNameWidth_, instWidth_;
    int volWidth_;
    int effWidth_, effIDWidth_, effValWidth_;

    // impl

    /// Called by main function.
    void set_history(history::History & history) {
        this->history = history;
    }

    /// Unsure if useful or not.
    void unset_history() {
        this->history = dummy_history;
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
