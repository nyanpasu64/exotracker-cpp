#ifndef PATTERNEDITORPANEL_H
#define PATTERNEDITORPANEL_H

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


namespace gui {
namespace pattern_editor {

class PatternEditorPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PatternEditorPanel(QWidget *parent = nullptr);

signals:

public slots:

public:
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

    //    void initDisplay();
    //    void drawPattern(const QRect& rect);

protected:
    // overrides QWidget
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent* event) override;
};

#endif // PATTERNEDITORPANEL_H

// namespaces
}
}