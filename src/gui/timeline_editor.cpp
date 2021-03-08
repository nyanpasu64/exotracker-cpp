#include "timeline_editor.h"
#include "doc.h"
#include "gui/lib/layout_macros.h"

#include <verdigris/wobjectimpl.h>

// Widgets
#include <QListView>

// Layouts
#include <QVBoxLayout>

// Other
#include <QString>

namespace gui::timeline_editor {
W_OBJECT_IMPL(TimelineEditor)

/// QAbstractItemModel is confusing.
/// This is based off
/// https://doc.qt.io/qt-5/model-view-programming.html#a-read-only-example-model.
class HistoryWrapper : public QAbstractListModel {
    W_OBJECT(HistoryWrapper)
public:
    GetDocument _get_document;

// impl
    HistoryWrapper(GetDocument get_document)
        : _get_document(get_document)
    {}

    [[nodiscard]] doc::Document const & get_document() const {
        return _get_document();
    }

    void set_history(GetDocument get_document) {
        beginResetModel();
        _get_document = get_document;
        endResetModel();
    }

    // impl QAbstractListModel
    [[nodiscard]] int rowCount(QModelIndex const & parent) const override {
        return int(get_document().timeline.size());
    }

    [[nodiscard]] QVariant data(QModelIndex const & index, int role) const override {
        auto & x = get_document().timeline;

        if (!index.isValid())
            return QVariant();

        if ((size_t) index.row() >= x.size())
            return QVariant();

        if (role == Qt::DisplayRole)
            return QString::number(index.row());
        else
            return QVariant();
    }

    /// Binds win's cursor y position, to this model.
    QModelIndex get_cursor_y_from(MainWindow const& win) {
        return createIndex((int) win._cursor.get().y.grid, 0);
    }
};
W_OBJECT_IMPL(HistoryWrapper)

class TimelineEditorImpl : public TimelineEditor {
    W_OBJECT(TimelineEditorImpl)
public:
    MainWindow & _win;

    HistoryWrapper _model;
    QListView * _widget;

    // impl
    explicit TimelineEditorImpl(MainWindow * win, QWidget * parent)
        : TimelineEditor(parent)
        , _win(*win)
        , _model(GetDocument::empty())
    {
        auto c = this;
        auto l = new QVBoxLayout(c);
        setLayout(l);

        l->setContentsMargins(0, 0, 0, 0);

        {
            l__w(QListView);
            _widget = w;
            w->setDisabled(true);
        }

        _widget->setModel(&_model);
    }

    [[nodiscard]] doc::Document const & get_document() const {
        return _model.get_document();
    }

    void set_history(GetDocument get_document) override {
        _model.set_history(get_document);
        update_cursor();
    }

    void update_cursor() override {
        QModelIndex order_y = _model.get_cursor_y_from(_win);

        QItemSelectionModel & widget_select = *_widget->selectionModel();
        widget_select.select(order_y, QItemSelectionModel::ClearAndSelect);
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
