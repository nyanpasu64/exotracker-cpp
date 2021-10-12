#include "instrument_list.h"
#include "doc.h"
#include "gui/lib/dpi.h"
#include "gui/lib/format.h"
#include "gui/lib/icon_toolbar.h"
#include "gui/lib/instr_warnings.h"
#include "gui/lib/layout_macros.h"
#include "gui/lib/list_warnings.h"
#include "util/unwrap.h"
#include "edit/edit_instr_list.h"

#include <verdigris/wobjectimpl.h>

// Widgets
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>

// Layouts
#include <QVBoxLayout>

// Other
#include <QAbstractListModel>
#include <QAction>
#include <QDebug>
#include <QMimeData>
#include <QSignalBlocker>

#include <utility>

namespace gui::instrument_list {
W_OBJECT_IMPL(InstrumentList)

using doc::InstrumentIndex;
using gui::lib::dpi::dpi_scale;
using gui::lib::instr_warnings::KeysplitWarningIter;
using gui::lib::instr_warnings::PatchWarnings;
using namespace gui::lib::list_warnings;
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
    MainWindow * _win;
    DragAction _drag_action = DragAction::Swap;

    std::vector<QString> _instr_warnings;
    QIcon _warning_icon;
    QColor _warning_color;

// impl
public:
    InstrumentListModel(MainWindow * win)
        : _win(win)
        , _warning_icon(warning_icon())
        , _warning_color(warning_bg())
    {
        _instr_warnings.resize(doc::MAX_INSTRUMENTS);
    }

    [[nodiscard]] doc::Document const & get_document() const {
        return _win->state().document();
    }

    void reload_state() {
        // TODO move the call to beginResetModel() to a signal emitted when
        // StateTransaction::history_mut() is first called.
        beginResetModel();

        doc::Document const& doc = get_document();

        for (size_t instr_idx = 0; instr_idx < doc::MAX_INSTRUMENTS; instr_idx++) {
            auto const& instr = doc.instruments[instr_idx];
            if (!instr.has_value()) {
                _instr_warnings[instr_idx] = QString();
                continue;
            }

            std::vector<QString> all_warnings;

            auto warning_iter = KeysplitWarningIter(doc, *instr);
            while (auto w = warning_iter.next()) {
                for (QString const& s : w->warnings) {
                    all_warnings.push_back(tr("Patch %1: %2").arg(w->patch_idx).arg(s));
                }
            }

            if (instr->keysplit.empty()) {
                // TODO move string and translation to instr_warnings.h/cpp
                all_warnings.push_back(tr("No keysplits found"));
            }

            _instr_warnings[instr_idx] = warning_tooltip(all_warnings);
        }

        endResetModel();
    }

    bool has_warning(size_t row) const {
        return !_instr_warnings[row].isEmpty();
    }

// impl QAbstractItemModel
public:
    int rowCount(QModelIndex const & parent) const override {
        if (parent.isValid()) {
            // Rows do not have children.
            return 0;
        } else {
            // The root has items.
            return int(get_document().instruments.size());
        }
    }

    QVariant data(QModelIndex const & index, int role) const override {
        auto & instruments = get_document().instruments;

        if (!index.isValid() || index.parent().isValid())
            return QVariant();

        auto row = (size_t) index.row();
        assert(instruments.size() ==_instr_warnings.size());
        if (row >= instruments.size())
            return QVariant();

        switch (role) {
        case Qt::DisplayRole:
            if (instruments[row]) {
                return QStringLiteral("%1 - %2").arg(
                    format_hex_2(row), QString::fromStdString(instruments[row]->name)
                );
            } else {
                return format_hex_2(row);
            }

        case Qt::DecorationRole:
            if (has_warning(row)) {
                return _warning_icon;
            } else {
                return QVariant();
            }

        case Qt::ToolTipRole:
            return _instr_warnings[row];

        case Qt::BackgroundRole:
            if (has_warning(row)) {
                return _warning_color;
            } else {
                return QVariant();
            }

        default:
            return QVariant();
        }
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

/// Automatically computes a uniform grid based on its item sizes.
/// Clamps the grid size between a minimum and maximum width.
class GridListView : public QListView {
public:
    explicit GridListView(QWidget * parent = nullptr)
        : QListView(parent)
    {
        setWrapping(true);
    }

    static constexpr int MIN_WIDTH = 40;
    static constexpr int MAX_WIDTH = 128;

    void doItemsLayout() override {
        QStyleOptionViewItem option = viewOptions();
        auto model = this->model();
        if (!model || !itemDelegate()) {
            return QListView::doItemsLayout();
        }

        // If no items, use default invalid size.
        QSize size;

        int nrows = model->rowCount();
        for (int row = 0; row < nrows; row++) {
            if (isRowHidden(row)) {
                continue;
            }
            auto index = model->index(row, 0);
            auto delegate = itemDelegate(index);

            size = size.expandedTo(delegate->sizeHint(option, index));
        }

        // If items present, clamp size within minimum/maximum width.
        // If no items present, disable fixed grid.
        if (size.isValid()) {
            int scaled_min_width = qRound(dpi_scale(this, MIN_WIDTH));
            int scaled_max_width = qRound(dpi_scale(this, MAX_WIDTH));
            size.setWidth(std::clamp(size.width(), scaled_min_width, scaled_max_width));
        }

        setGridSize(size);
        QListView::doItemsLayout();
    }
};

using gui::lib::icon_toolbar::enable_button_borders;

using main_window::StateComponent;

class InstrumentListImpl final : public InstrumentList {
    W_OBJECT(InstrumentListImpl)
public:
    MainWindow & _win;
    InstrumentListModel _model;
    bool _show_empty_slots = false;

    // Widgets
    GridListView * _list;
    QToolBar * _tb;
    QLineEdit * _rename;

    // Actions
    QAction * _add;
    QAction * _remove;
    QAction * _edit;
    QAction * _clone;
    // TODO add export/import buttons
    QAction * _show_empty;

    explicit InstrumentListImpl(MainWindow * win, QWidget * parent)
        : InstrumentList(parent)
        , _win(*win)
        , _model(win)
    {
        auto c = this;
        auto l = new QVBoxLayout(c);
        setLayout(l);

        l->setContentsMargins(0, 0, 0, 0);

        {l__w(GridListView);
            _list = w;
            w->setFocusPolicy(Qt::TabFocus);
            w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        }
        {l__l(QHBoxLayout);
            {l__w(QToolBar);
                _tb = w;

                _add = w->addAction("+");
                _remove = w->addAction("x");
                _edit = w->addAction("✏️");
                _clone = w->addAction("C");
                w->addSeparator();
                _show_empty = w->addAction("_");

                _show_empty->setCheckable(true);

                enable_button_borders(w);
            }
            {l__w(QLineEdit);
                _rename = w;
            }
        }

        // Widget holds a reference, does *not* take ownership.
        // If widget is destroyed first, it doesn't affect the model.
        // If model is destroyed first, its destroyed() signal disconnects all widgets using it.
        _list->setModel(&_model);
        _list->setIconSize(ICON_SIZE);

        _list->setDragEnabled(true);
        _list->setAcceptDrops(true);

        // See the comment in InstrumentListModel for an explanation of DragDropMode.
        _list->setDragDropMode(QListView::InternalMove);
        _list->setDragDropOverwriteMode(true);
        _list->setDropIndicatorShown(true);

        // Connect instrument list.
        connect(
            _list->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &InstrumentListImpl::on_selection_changed);
        connect(
            _list, &QListView::doubleClicked,
            this, &InstrumentListImpl::on_edit_instrument);

        // Enable right-click menus for instrument list.
        _list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(
            _list, &QWidget::customContextMenuRequested,
            this, &InstrumentListImpl::on_right_click);

        // Connect toolbar.
        connect(
            _add, &QAction::triggered,
            this, &InstrumentListImpl::on_add);
        connect(
            _remove, &QAction::triggered,
            this, &InstrumentListImpl::on_remove);
        connect(
            _edit, &QAction::triggered,
            this, &InstrumentListImpl::on_edit_instrument);
        connect(
            _clone, &QAction::triggered,
            this, &InstrumentListImpl::on_clone);
        connect(
            _show_empty, &QAction::toggled,
            this, &InstrumentListImpl::on_show_empty);

        connect(
            _rename, &QLineEdit::textEdited,
            this, &InstrumentListImpl::on_rename);
    }

    doc::Document const& document() const {
        return _win.state().document();
    }

    InstrumentIndex curr_instr_idx() const {
        return _win.state().instrument();
    }

    void reload_state() override {
        _model.reload_state();
        recompute_visible_slots();
        update_selection();
    }

    void recompute_visible_slots() {
        auto & instruments = _model.get_document().instruments;
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
        auto instr_idx = curr_instr_idx();
        auto const& instr = document().instruments[instr_idx];

        auto idx = _model.index((int) instr_idx, 0);

        {
            QItemSelectionModel & list_select = *_list->selectionModel();
            // _list->selectionModel() merely responds to the active instrument.
            // Block signals when we change it to match the active instrument.
            auto b = QSignalBlocker(list_select);
            list_select.select(idx, QItemSelectionModel::ClearAndSelect);
        }

        // Hack to avoid scrolling a widget before it's shown
        // (which causes broken layout and crashes).
        // This probably won't have any bad effects,
        // since when the app starts, the instrument number is always 0,
        // and even if it was nonzero, only the scrolling will be wrong,
        // not the actual selected instrument (which could cause a desync).
        if (isVisible()) {
            _list->scrollTo(idx);
        }

        _remove->setEnabled(instr.has_value());
        _edit->setEnabled(instr.has_value());
        _clone->setEnabled(instr.has_value());
        _rename->setEnabled(instr.has_value());

        {
            auto b = QSignalBlocker(_rename);
            if (instr) {
                auto name = QString::fromStdString(instr->name);
                if (_rename->text() != name) {
                    _rename->setText(std::move(name));
                }
            } else {
                _rename->clear();
            }
        }
    }

    void on_selection_changed(QItemSelection const& selection) {
        // Only 1 element can be selected at once, or 0 if you ctrl+click.
        assert(selection.size() <= 1);
        if (!selection.empty()) {
            debug_unwrap(_win.edit_state(), [&](auto & tx) {
                tx.set_instrument(selection[0].top());
            });
        }
    }

    void on_right_click(QPoint const& pos) {
        auto index = _list->indexAt(pos);
        std::optional<InstrumentIndex> instr_idx;
        if (index.isValid()) {
            release_assert((size_t) index.row() < doc::MAX_INSTRUMENTS);
            instr_idx = (InstrumentIndex) index.row();
        }

        auto const& instruments = document().instruments;

        auto menu = new QMenu(_list);
        menu->setAttribute(Qt::WA_DeleteOnClose);

        auto add = menu->addAction(tr("&Add Instrument"));
        connect(
            add, &QAction::triggered,
            this, index.isValid()
                ? &InstrumentListImpl::on_add
                : &InstrumentListImpl::add_at_begin);

        if (instr_idx && instruments[*instr_idx].has_value()) {
            {
                auto remove = menu->addAction(tr("&Remove Instrument"));
                connect(
                    remove, &QAction::triggered,
                    this, &InstrumentListImpl::on_remove);
            }
            {
                auto clone = menu->addAction(tr("&Clone Instrument"));
                connect(
                    clone, &QAction::triggered,
                    this, &InstrumentListImpl::on_clone);
            }
            menu->addSeparator();
            {
                auto edit = menu->addAction(tr("&Edit..."));
                connect(
                    edit, &QAction::triggered,
                    this, &InstrumentListImpl::on_edit_instrument);
            }
        }

        menu->popup(_list->viewport()->mapToGlobal(pos));
    }

    void on_edit_instrument() {
        if (document().instruments[curr_instr_idx()].has_value()) {
            _win.show_instr_dialog();
        }
    }

    void on_add() {
        // If empty slots are visible, allow initializing instruments in empty slots
        // through the toolbar, instead of only through the right-click menu.
        add_instrument(_show_empty_slots ? curr_instr_idx() : 0);
    }

    void add_instrument(InstrumentIndex begin_idx) {
        using edit::edit_instr_list::try_add_instrument;

        auto [maybe_edit, new_instr] = try_add_instrument(document(), begin_idx);
        if (!maybe_edit) {
            return;
        }

        auto tx = _win.edit_unwrap();
        tx.push_edit(std::move(maybe_edit), IGNORE_CURSOR);
        tx.set_instrument(new_instr);
    }

    void add_at_begin() {
        add_instrument(0);
    }

    void on_remove() {
        using edit::edit_instr_list::try_remove_instrument;

        auto [maybe_edit, new_instr] =
            try_remove_instrument(document(), curr_instr_idx());
        if (!maybe_edit) {
            return;
        }

        auto tx = _win.edit_unwrap();
        tx.push_edit(std::move(maybe_edit), IGNORE_CURSOR);
        tx.instrument_deleted();

        // If empty slots are hidden, removing an instrument hides it from the list.
        // To keep the cursor in place, move the cursor to the next visible instrument.
        if (!_show_empty_slots) {
            tx.set_instrument(new_instr);
        }
    }

    void on_clone() {
        clone_instrument(_show_empty_slots ? curr_instr_idx() : 0);
    }

    void clone_instrument(InstrumentIndex begin_idx) {
        using edit::edit_instr_list::try_clone_instrument;

        auto [maybe_edit, new_instr] =
            try_clone_instrument(document(), curr_instr_idx(), begin_idx);
        if (!maybe_edit) {
            return;
        }

        auto tx = _win.edit_unwrap();
        tx.push_edit(std::move(maybe_edit), IGNORE_CURSOR);
        tx.set_instrument(new_instr);
    }

    void on_show_empty(bool show) {
        _show_empty_slots = show;
        recompute_visible_slots();
    }

    void on_rename(QString const& qname) {
        using edit::edit_instr_list::try_rename_instrument;

        auto maybe_edit =
            try_rename_instrument(document(), curr_instr_idx(), qname.toStdString());
        if (!maybe_edit) {
            return;
        }

        auto tx = _win.edit_unwrap();
        tx.push_edit(std::move(maybe_edit), IGNORE_CURSOR);
    }
};
W_OBJECT_IMPL(InstrumentListImpl)
}

InstrumentList * InstrumentList::make(MainWindow * win, QWidget * parent) {
    return new InstrumentListImpl(win, parent);
}

// namespace
}
