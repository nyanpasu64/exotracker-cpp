#include "instrument_list.h"
#include "doc.h"
#include "gui/lib/format.h"
#include "gui/lib/layout_macros.h"
#include "util/unwrap.h"

#include <verdigris/wobjectimpl.h>

// Widgets
#include <QListView>

// Layouts
#include <QVBoxLayout>

// Other
#include <QSignalBlocker>
#include <QAbstractListModel>
#include <QSortFilterProxyModel>

namespace gui::instrument_list {
W_OBJECT_IMPL(InstrumentList)

namespace InstrumentListRole {
enum : int {
    Color = Qt::BackgroundRole,  // QBrush
    IsEnabled = Qt::UserRole,  // bool
};
}

using gui::lib::format::format_hex_2;

namespace {
class InstrumentListModel : public QAbstractListModel {
    W_OBJECT(InstrumentListModel)
public:
    GetDocument _get_document;

// impl
    InstrumentListModel(GetDocument get_document)
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
    int rowCount(QModelIndex const & /*parent*/) const override {
        return int(get_document().instruments.v.size());
    }

    QVariant data(QModelIndex const & index, int role) const override {
        auto & x = get_document().instruments.v;

        if (!index.isValid())
            return QVariant();

        auto row = (size_t) index.row();
        if (row >= x.size())
            return QVariant();

        if (role == Qt::DisplayRole) {
            if (x[row].has_value()) {
                return QStringLiteral("%1 - %2").arg(
                    format_hex_2(row), QString::fromStdString(x[row]->name)
                );
            } else {
                return format_hex_2(row);
            }
        }
        if (role == InstrumentListRole::IsEnabled) {
            return x[row].has_value();
        }

        return QVariant();
    }
};
W_OBJECT_IMPL(InstrumentListModel)

class InstrumentListFilter : public QSortFilterProxyModel {
    bool _show_empty_slots = false;

public:
    void show_empty_slots(bool show) {
        if (show != _show_empty_slots) {
            _show_empty_slots = show;
            invalidateFilter();
        }
    }

    // impl QSortFilterProxyModel
    protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &) const override {
        if (_show_empty_slots) {
            return true;
        }
        return sourceModel()->data(
            sourceModel()->index(source_row, 0), InstrumentListRole::IsEnabled
        ).toBool();
    }
};

class InstrumentListImpl : public InstrumentList {
    W_OBJECT(InstrumentListImpl)
public:
    MainWindow & _win;

    InstrumentListModel _source_model;
    InstrumentListFilter _filter_model;
    QListView * _widget;

    explicit InstrumentListImpl(MainWindow * win, QWidget * parent)
        : InstrumentList(parent)
        , _win(*win)
        , _source_model(GetDocument::empty())
    {
        auto c = this;
        auto l = new QVBoxLayout(c);
        setLayout(l);

        l->setContentsMargins(0, 0, 0, 0);

        {
            l__w(QListView);
            _widget = w;
            w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
            w->setWrapping(true);
        }

        // _filter_model holds a reference, does *not* take ownership.
        // If filter is destroyed first, it doesn't affect the source.
        // If source is destroyed first, its destroyed() signal disconnects the filter mode.
        // _filter_model is destroyed before _source_model (comes after in the list of fields).
        _filter_model.setSourceModel(&_source_model);

        // Widget holds a reference, does *not* take ownership.
        // If widget is destroyed first, it doesn't affect the model.
        // If model is destroyed first, its destroyed() signal disconnects all widgets using it.
        _widget->setModel(&_filter_model);

        connect(
            _widget->selectionModel(), &QItemSelectionModel::selectionChanged,
            win, [this, win](const QItemSelection &filter_sel, const QItemSelection &) {
                // Only 1 element can be selected at once, or 0 if you ctrl+click.
                assert(filter_sel.size() <= 1);
                QItemSelection source_sel = _filter_model.mapSelectionToSource(filter_sel);
                if (!source_sel.empty()) {
                    debug_unwrap(win->edit_state(), [&](auto & tx) {
                        tx.set_instrument(source_sel[0].top());
                    });
                }
            });

        connect(
            _widget, &QListView::doubleClicked,
            this, &InstrumentListImpl::on_double_click);
    }

    void set_history(GetDocument get_document) override {
        _source_model.set_history(get_document);
        update_selection();
    }

    void update_selection() override {
        auto source_idx = _source_model.index(_win._state.instrument(), 0);
        auto filter_idx = _filter_model.mapFromSource(source_idx);

        QItemSelectionModel & widget_select = *_widget->selectionModel();
        // _widget->selectionModel() merely responds to the active instrument.
        // Block signals when we change it to match the active instrument.
        auto s = QSignalBlocker(widget_select);
        widget_select.select(filter_idx, QItemSelectionModel::ClearAndSelect);

        // Hack to avoid scrolling a widget before it's shown
        // (which causes broken layout and crashes).
        // This probably won't have any bad effects,
        // since when the app starts, the instrument number is always 0,
        // and even if it was nonzero, only the scrolling will be wrong,
        // not the actual selected instrument (which could cause a desync).
        if (isVisible()) {
            _widget->scrollTo(filter_idx);
        }
    }

    void on_double_click(QModelIndex const& filter_idx) {
        _win.show_instr_dialog();
    }
};
W_OBJECT_IMPL(InstrumentListImpl)
}

InstrumentList * InstrumentList::make(MainWindow * win, QWidget * parent) {
    return new InstrumentListImpl(win, parent);
}

// namespace
}
