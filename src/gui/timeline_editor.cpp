#include "timeline_editor.h"
#include "doc.h"
#include "gui/lib/layout_macros.h"

#include <verdigris/wobjectimpl.h>

// Widgets
#include <QLabel>

// Layouts
#include <QVBoxLayout>

// Other
#include <QString>

namespace gui::timeline_editor {
W_OBJECT_IMPL(TimelineEditor)

class TimelineEditorImpl : public TimelineEditor {
    W_OBJECT(TimelineEditorImpl)
public:
    MainWindow & _win;

    // impl
    explicit TimelineEditorImpl(MainWindow * win, QWidget * parent)
        : TimelineEditor(parent)
        , _win(*win)
    {
        auto c = this;
        auto l = new QVBoxLayout(c);
        setLayout(l);

        l->setContentsMargins(0, 0, 0, 0);

        {
            l__w(QLabel(tr("TODO steal from klystrack or sth")));
            w->setWordWrap(true);
        }
    }

    void set_history(GetDocument get_document) override {
    }

    void update_cursor() override {
    }

    QSize sizeHint() const override {
        return {0, 0};
    }
};
W_OBJECT_IMPL(TimelineEditorImpl)

TimelineEditor * TimelineEditor::make(MainWindow * win, QWidget * parent) {
    return new TimelineEditorImpl(win, parent);
}

}
