#include "instrument_list.h"
#include "doc.h"
#include "gui/lib/layout_macros.h"
#include "util/unwrap.h"

#include <verdigris/wobjectimpl.h>

// Widgets
#include <QListView>

// Layouts
#include <QVBoxLayout>

// Other
#include <QSignalBlocker>

namespace gui::instrument_list {
W_OBJECT_IMPL(InstrumentList)

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
    int rowCount(QModelIndex const & parent) const override {
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
                return QStringLiteral("%1 - %2")
                    .arg(row, 2, 16, QLatin1Char('0'))
                    .arg(QString::fromStdString(x[row]->name));
            } else {
                return QStringLiteral("%1").arg(row, 2, 16, QLatin1Char('0'));
            }
        } else
            return QVariant();
    }
};
W_OBJECT_IMPL(InstrumentListModel)

class InstrumentListImpl : public InstrumentList {
    W_OBJECT(InstrumentListImpl)
public:
    MainWindow & _win;

    InstrumentListModel _model;
    QListView * _widget;

    explicit InstrumentListImpl(MainWindow * win, QWidget * parent)
        : InstrumentList(parent)
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
            w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
            w->setWrapping(true);
        }

        _widget->setModel(&_model);
        connect(
            _widget->selectionModel(), &QItemSelectionModel::selectionChanged,
            win, [win](const QItemSelection &selected, const QItemSelection &) {
                // Only 1 element can be selected at once, or 0 if you ctrl+click.
                assert(selected.size() <= 1);
                if (!selected.empty()) {
                    debug_unwrap(win->edit_state(), [&](auto & tx) {
                        tx.set_instrument(selected[0].top());
                    });
                }
            });
    }

    [[nodiscard]] doc::Document const & get_document() const {
        return _model.get_document();
    }

    void set_history(GetDocument get_document) override {
        _model.set_history(get_document);
        update_selection();
    }

    void update_selection() override {
        QModelIndex instr_idx = _model.index(_win._state.instrument(), 0);

        QItemSelectionModel & widget_select = *_widget->selectionModel();
        // _widget->selectionModel() merely responds to the active instrument.
        // Block signals when we change it to match the active instrument.
        auto s = QSignalBlocker(widget_select);
        widget_select.select(instr_idx, QItemSelectionModel::ClearAndSelect);

        // Hack to avoid scrolling a widget before it's shown
        // (which causes broken layout and crashes).
        // This probably won't have any bad effects,
        // since when the app starts, the instrument number is always 0,
        // and even if it was nonzero, only the scrolling will be wrong,
        // not the actual selected instrument (which could cause a desync).
        if (isVisible()) {
            _widget->scrollTo(instr_idx);
        }
    }
};
W_OBJECT_IMPL(InstrumentListImpl)
}

InstrumentList * InstrumentList::make(MainWindow * win, QWidget * parent) {
    return new InstrumentListImpl(win, parent);
}

// namespace
}
