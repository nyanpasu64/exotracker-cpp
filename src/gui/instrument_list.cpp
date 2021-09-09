#include "instrument_list.h"
#include "doc.h"
#include "gui/lib/format.h"
#include "gui/lib/layout_macros.h"
#include "util/unwrap.h"
#include "edit/edit_instr_list.h"

#include <verdigris/wobjectimpl.h>

// Widgets
#include <QListView>

// Layouts
#include <QVBoxLayout>

// Other
#include <QAbstractListModel>
#include <QDebug>
#include <QMimeData>
#include <QSignalBlocker>

namespace gui::instrument_list {
W_OBJECT_IMPL(InstrumentList)

using doc::InstrumentIndex;
using main_window::MoveCursor_::IGNORE_CURSOR;

using gui::lib::format::format_hex_2;

namespace {
enum class DragAction {
    /// Dragging an instrument swaps the source and destination.
    Swap,
    /// Dragging an instrument moves the source into a gap between instruments (not
    /// implemented yet).
    Move,
};

class InstrumentListModel final : public QAbstractListModel {
    W_OBJECT(InstrumentListModel)
private:
    // TODO ...?
    MainWindow * _win;
    GetDocument _get_document;
    DragAction _drag_action = DragAction::Swap;

// impl
public:
    InstrumentListModel(MainWindow * win, GetDocument get_document)
        : _win(win)
        , _get_document(get_document)
    {}

    [[nodiscard]] doc::Document const & get_document() const {
        return _get_document();
    }

    void set_history(GetDocument get_document) {
        beginResetModel();
        _get_document = get_document;
        endResetModel();
    }

// impl QAbstractItemModel
public:
    int rowCount(QModelIndex const & parent) const override {
        if (parent.isValid()) {
            // Rows do not have children.
            return 0;
        } else {
            // The root has items.
            return int(get_document().instruments.v.size());
        }
    }

    QVariant data(QModelIndex const & index, int role) const override {
        auto & instruments = get_document().instruments.v;

        if (!index.isValid() || index.parent().isValid())
            return QVariant();

        auto row = (size_t) index.row();
        if (row >= instruments.size())
            return QVariant();

        if (role == Qt::DisplayRole) {
            if (instruments[row].has_value()) {
                return QStringLiteral("%1 - %2").arg(
                    format_hex_2(row), QString::fromStdString(instruments[row]->name)
                );
            } else {
                return format_hex_2(row);
            }
        }

        return QVariant();
    }

    /*
    Qt drag and drop is byzantine, like CMake.

    QAbstractItemView::startDrag() calls QDrag::exec(), which serializes the drag
    origin into MIME and calls QListView::dropEvent() when you release the mouse.
    When you drag an item between items, QListView::dropEvent() calls
    InstrumentListModel::moveRows() and returns.

    If you instead drag an item onto another item using Qt::MoveAction, Qt is designed
    to overwrite the target with the source and then erase the source:
    QListView::dropEvent() falls through to QAbstractItemView::dropEvent() which calls
    InstrumentListModel::dropMimeData(). Afterwards, QAbstractItemView::startDrag()
    calls InstrumentListModel::removeRows() to remove the origin of the drag.

    However I want dragging an item onto another item to instead swap them.
    I have two options:

    - Qt::CopyAction is easy to work with, but requires QListView::DragDrop (which
      allows users to drag items *between* unrelated widgets, in which case we
      erroneously index into InstrumentListModel using an item dragged from an
      unrelated model).
    - QListView::InternalMove prevents cross-widget dragging, but doesn't support
      Qt::CopyAction but only Qt::MoveAction, which sends a spurious removeRows() we
      must ignore).

    I decided to pick InternalMove because it's the easiest way to ensure local
    reasoning.

    ----

    How do we control whether an item is dropped onto the nearest item (to swap),
    onto the nearest gap between items (to move), or both? This is determined by
    QAbstractItemViewPrivate::position():

    - If QAIM::flags() called with a valid index omits Qt::ItemIsDropEnabled,
      then QListView will only drag onto a gap between items.
    - Otherwise, if QListWidget::setDragDropOverwriteMode(true) is called,
      then QListView will only drag onto an item.
    - If neither is the case, then QListView will allow both (which is hard for the
      user to control because the gap between items is very thin).

    My approach is to call setDragDropOverwriteMode(true) to ensure large hitboxes,
    then use QAIM::flags() to control whether the user drags onto or between items.

    ----

    Note that flags() can't tell the difference between dragging *between* items and
    *after* the last item, since both appear as invalid indexes. Dragging after the
    last item calls moveRows() with destinationChild = rowCount() (AKA dragging to
    after the final item).

    If I ever implement instrument reordering, this should be clamped to "last
    non-empty slot + 1" if empty slots are hidden, because nobody deliberately intends
    to move an instrument to after slot FF.
    */

    Qt::ItemFlags flags(QModelIndex const& index) const override {
        Qt::ItemFlags flags = QAbstractListModel::flags(index);
        if (index.isValid()) {
            flags |= Qt::ItemIsDragEnabled;
        }

        // If we're in swap mode, only allow dropping *onto* items.
        if (_drag_action == DragAction::Swap && index.isValid()) {
            flags |= Qt::ItemIsDropEnabled;
        }

        // If we're in move mode, only allow dropping *between* items. (This also
        // allows dropping in the background, which acts like dragging past the
        // final row.)
        if (_drag_action == DragAction::Move && !index.isValid()) {
            flags |= Qt::ItemIsDropEnabled;
        }

        return flags;
    }

    Qt::DropActions supportedDragActions() const override {
        return Qt::MoveAction;
    }

    Qt::DropActions supportedDropActions() const override {
        return Qt::MoveAction;
    }

    // TODO when I add move-row support, override moveRows() and create an EditBox
    // when called.

    bool dropMimeData(
        QMimeData const* data,
        Qt::DropAction action,
        int insert_row,
        int insert_column,
        QModelIndex const& replace_index)
    override {
        using edit::edit_instr_list::swap_instruments;

        // Based off QAbstractListModel::dropMimeData().
        if (!data || !(action == Qt::CopyAction || action == Qt::MoveAction))
            return false;

        QStringList types = mimeTypes();
        if (types.isEmpty())
            return false;
        QString format = types.at(0);
        if (!data->hasFormat(format))
            return false;

        QByteArray encoded = data->data(format);
        QDataStream stream(&encoded, QIODevice::ReadOnly);

        // if the drop is on an item, swap the dragged and dropped items.
        if (replace_index.isValid() && insert_row == -1 && insert_column == -1) {
            int drag_row;
            stream >> drag_row;

            int replace_row = replace_index.row();

            assert((size_t) drag_row < doc::MAX_INSTRUMENTS);
            assert((size_t) replace_row < doc::MAX_INSTRUMENTS);
            if ((size_t) drag_row >= doc::MAX_INSTRUMENTS) {
                return false;
            }
            if ((size_t) replace_row >= doc::MAX_INSTRUMENTS) {
                return false;
            }

            {
                auto tx = _win->edit_unwrap();
                tx.push_edit(
                    swap_instruments(
                        InstrumentIndex(drag_row), InstrumentIndex(replace_row)
                    ),
                    IGNORE_CURSOR);
                tx.set_instrument(replace_row);
            }
            return true;
        }

        return false;
    }

    /// removeRows() is called by QAbstractItemView::startDrag() when the user drags
    /// two items to swap them. But we want to swap items, not overwrite one with
    /// another. So ignore the call.
    bool removeRows(int row, int count, const QModelIndex & parent) override {
        return false;
    }
};
W_OBJECT_IMPL(InstrumentListModel)

class InstrumentListImpl final : public InstrumentList {
    W_OBJECT(InstrumentListImpl)
public:
    MainWindow & _win;
    InstrumentListModel _model;
    bool _show_empty_slots = false;

    // Widgets
    QListView * _list;

    explicit InstrumentListImpl(MainWindow * win, QWidget * parent)
        : InstrumentList(parent)
        , _win(*win)
        , _model(win, GetDocument::empty())
    {
        auto c = this;
        auto l = new QVBoxLayout(c);
        setLayout(l);

        l->setContentsMargins(0, 0, 0, 0);

        {l__w(QListView);
            _list = w;
            w->setFocusPolicy(Qt::TabFocus);
            w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
            w->setWrapping(true);
        }

        // Widget holds a reference, does *not* take ownership.
        // If widget is destroyed first, it doesn't affect the model.
        // If model is destroyed first, its destroyed() signal disconnects all widgets using it.
        _list->setModel(&_model);

        _list->setDragEnabled(true);
        _list->setAcceptDrops(true);

        // See the comment in InstrumentListModel for an explanation of DragDropMode.
        _list->setDragDropMode(QListView::InternalMove);
        _list->setDragDropOverwriteMode(true);
        _list->setDropIndicatorShown(true);

        connect(
            _list->selectionModel(), &QItemSelectionModel::selectionChanged,
            win, [win](const QItemSelection &selection, const QItemSelection &) {
                // Only 1 element can be selected at once, or 0 if you ctrl+click.
                assert(selection.size() <= 1);
                if (!selection.empty()) {
                    debug_unwrap(win->edit_state(), [&](auto & tx) {
                        tx.set_instrument(selection[0].top());
                    });
                }
            });

        connect(
            _list, &QListView::doubleClicked,
            this, &InstrumentListImpl::on_double_click);
    }

    // it's a nasty hack that we set history to reload changes from a StateTransaction,
    // but it works don't touch it
    void set_history(GetDocument get_document) override {
        _model.set_history(get_document);
        recompute_visible_slots();
        update_selection();
    }

    void recompute_visible_slots() {
        auto & instruments = _model.get_document().instruments.v;
        int nrow = _model.rowCount({});

        if (_show_empty_slots) {
            for (int row = 0; row < nrow; row++) {
                _list->setRowHidden(row, false);
            }
        } else {
            for (int row = 0; row < nrow; row++) {
                _list->setRowHidden(row, !instruments[(size_t) row].has_value());
            }
        }
    }

    void update_selection() override {
        auto idx = _model.index(_win._state.instrument(), 0);

        QItemSelectionModel & list_select = *_list->selectionModel();
        // _list->selectionModel() merely responds to the active instrument.
        // Block signals when we change it to match the active instrument.
        auto s = QSignalBlocker(list_select);
        list_select.select(idx, QItemSelectionModel::ClearAndSelect);

        // Hack to avoid scrolling a widget before it's shown
        // (which causes broken layout and crashes).
        // This probably won't have any bad effects,
        // since when the app starts, the instrument number is always 0,
        // and even if it was nonzero, only the scrolling will be wrong,
        // not the actual selected instrument (which could cause a desync).
        if (isVisible()) {
            _list->scrollTo(idx);
        }
    }

    void on_double_click(QModelIndex const& /*idx*/) {
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
