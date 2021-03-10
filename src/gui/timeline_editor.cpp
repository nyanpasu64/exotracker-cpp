#include "timeline_editor.h"
#include "doc.h"
#include "gui/lib/layout_macros.h"

// Widgets
#include <QListView>

// Layouts
#include <QVBoxLayout>

// Other
#include <QString>

namespace gui::timeline_editor {

/// QAbstractItemModel is confusing.
/// This is based off
/// https://doc.qt.io/qt-5/model-view-programming.html#a-read-only-example-model.
struct HistoryWrapper : QAbstractListModel {
    // Upon construction, history = dummy_history, until a document is created and assigned.
    History _dummy_history{doc::DocumentCopy{}};

    // Non-null.
    History const* _history = &_dummy_history;

    // impl
    [[nodiscard]] doc::Document const & get_document() const {
        return _history->get_document();
    }

    void set_history(History const& history) {
        auto old_n = int(get_document().timeline.size());
        if (old_n > 0) {
            beginRemoveRows({}, 0, old_n - 1);
            _history = &_dummy_history;
            endRemoveRows();
        }

        auto n = int(history.get_document().timeline.size());
        beginInsertRows({}, 0, n - 1);
        _history = &history;
        endInsertRows();
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

    QModelIndex cursor_y(MainWindow & win) {
        return createIndex((int) win._cursor.get().y.grid, 0);
    }
};

class TimelineEditorImpl : public TimelineEditor {
public:
    MainWindow & _win;

    HistoryWrapper _model;
    QListView * _widget;

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
            l__w(QListView);
            _widget = w;
            w->setDisabled(true);
        }

        _widget->setModel(&_model);
    }

    [[nodiscard]] doc::Document const & get_document() const {
        return _model.get_document();
    }

    void set_history(History const& history) override {
        update_cursor();
        _model.set_history(history);
    }

    void update_cursor() override {
        QModelIndex order_y = _model.cursor_y(_win);

        QItemSelectionModel & widget_select = *_widget->selectionModel();
        widget_select.select(order_y, QItemSelectionModel::ClearAndSelect);
    }

    QSize sizeHint() const override {
        return {0, 0};
    }
};

TimelineEditor * TimelineEditor::make(MainWindow * win, QWidget * parent) {
    return new TimelineEditorImpl(win, parent);
}

}
