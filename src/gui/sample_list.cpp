#include "sample_list.h"
#include "doc.h"
#include "gui/lib/dpi.h"
#include "gui/lib/format.h"
#include "gui/lib/icon_toolbar.h"
#include "gui/lib/layout_macros.h"
#include "util/unwrap.h"
#include "edit/edit_sample_list.h"

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

namespace gui::sample_list {
W_OBJECT_IMPL(SampleList)

using doc::SampleIndex;
using gui::lib::dpi::dpi_scale;
using main_window::MoveCursor_::IGNORE_CURSOR;

using gui::lib::format::format_hex_2;

namespace {
enum class DragAction {
    /// Dragging an sample swaps the source and destination.
    Swap,
    /// Dragging an sample moves the source into a gap between samples (not
    /// implemented yet).
    Move,
};

class SampleListModel final : public QAbstractListModel {
    // Based off InstrumentListModel.
    W_OBJECT(SampleListModel)
private:
    MainWindow * _win;
    DragAction _drag_action = DragAction::Swap;

// impl
public:
    SampleListModel(MainWindow * win)
        : _win(win)
    {}

    [[nodiscard]] doc::Document const & get_document() const {
        return _win->state().document();
    }

    void reload_state() {
        // TODO move the call to beginResetModel() to a signal emitted when
        // StateTransaction::history_mut() is first called.
        beginResetModel();
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
            return int(get_document().samples.size());
        }
    }

    QVariant data(QModelIndex const & index, int role) const override {
        auto & samples = get_document().samples;

        if (!index.isValid() || index.parent().isValid())
            return QVariant();

        auto row = (size_t) index.row();
        if (row >= samples.size())
            return QVariant();

        switch (role) {
        case Qt::DisplayRole:
            if (samples[row]) {
                return QStringLiteral("%1 - %2").arg(
                    format_hex_2(row), QString::fromStdString(samples[row]->name)
                );
            } else {
                return format_hex_2(row);
            }

        default:
            return QVariant();
        }
    }

    // See InstrumentListModel for drag-and-drop explanation.

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
        using edit::edit_sample_list::swap_samples;

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

            assert((size_t) drag_row < doc::MAX_SAMPLES);
            assert((size_t) replace_row < doc::MAX_SAMPLES);
            if ((size_t) drag_row >= doc::MAX_SAMPLES) {
                return false;
            }
            if ((size_t) replace_row >= doc::MAX_SAMPLES) {
                return false;
            }

            {
                auto tx = _win->edit_unwrap();
                tx.push_edit(
                    swap_samples(
                        SampleIndex(drag_row), SampleIndex(replace_row)
                    ),
                    IGNORE_CURSOR);
                tx.set_sample(replace_row);
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
W_OBJECT_IMPL(SampleListModel)

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

class SampleListImpl final : public SampleList {
    W_OBJECT(SampleListImpl)
public:
    MainWindow & _win;
    SampleListModel _model;
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

    explicit SampleListImpl(MainWindow * win, QWidget * parent)
        : SampleList(parent)
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

        _list->setDragEnabled(true);
        _list->setAcceptDrops(true);

        // See the comment in InstrumentListModel for an explanation of DragDropMode.
        _list->setDragDropMode(QListView::InternalMove);
        _list->setDragDropOverwriteMode(true);
        _list->setDropIndicatorShown(true);

        // Connect sample list.
        connect(
            _list->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SampleListImpl::on_selection_changed);
        connect(
            _list, &QListView::doubleClicked,
            this, &SampleListImpl::on_edit_sample);

        // Enable right-click menus for sample list.
        _list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(
            _list, &QWidget::customContextMenuRequested,
            this, &SampleListImpl::on_right_click);

        // Connect toolbar.
        connect(
            _add, &QAction::triggered,
            this, &SampleListImpl::on_add);
        connect(
            _remove, &QAction::triggered,
            this, &SampleListImpl::on_remove);
        connect(
            _edit, &QAction::triggered,
            this, &SampleListImpl::on_edit_sample);
        connect(
            _clone, &QAction::triggered,
            this, &SampleListImpl::on_clone);
        connect(
            _show_empty, &QAction::toggled,
            this, &SampleListImpl::on_show_empty);

        connect(
            _rename, &QLineEdit::textEdited,
            this, &SampleListImpl::on_rename);
    }

    doc::Document const& document() const {
        return _win.state().document();
    }

    SampleIndex curr_sample_idx() const {
        return _win.state().sample();
    }

    // it's a nasty hack that we set history to reload changes from a StateTransaction,
    // but it works don't touch it
    void reload_state() override {
        _model.reload_state();
        recompute_visible_slots();
        update_selection();
    }

    void recompute_visible_slots() {
        auto & samples = _model.get_document().samples;
        int nrow = _model.rowCount({});

        if (_show_empty_slots) {
            for (int row = 0; row < nrow; row++) {
                _list->setRowHidden(row, false);
            }
        } else {
            for (int row = 0; row < nrow; row++) {
                _list->setRowHidden(row, !samples[(size_t) row].has_value());
            }
        }
    }

    void update_selection() override {
        auto sample_idx = curr_sample_idx();
        auto const& sample = document().samples[sample_idx];

        auto idx = _model.index((int) sample_idx, 0);

        {
            QItemSelectionModel & list_select = *_list->selectionModel();
            // _list->selectionModel() merely responds to the active sample.
            // Block signals when we change it to match the active sample.
            auto b = QSignalBlocker(list_select);
            list_select.select(idx, QItemSelectionModel::ClearAndSelect);
        }

        _remove->setEnabled(sample.has_value());
        _edit->setEnabled(sample.has_value());
        _clone->setEnabled(sample.has_value());
        _rename->setEnabled(sample.has_value());

        {
            auto b = QSignalBlocker(_rename);
            if (sample) {
                auto name = QString::fromStdString(sample->name);
                if (_rename->text() != name) {
                    _rename->setText(std::move(name));
                }
            } else {
                _rename->clear();
            }
        }

        // Hack to avoid scrolling a widget before it's shown
        // (which causes broken layout and crashes).
        if (isVisible()) {
            _list->scrollTo(idx);
        }
    }

    void on_selection_changed(QItemSelection const& selection) {
        // Only 1 element can be selected at once, or 0 if you ctrl+click.
        assert(selection.size() <= 1);
        if (!selection.empty()) {
            debug_unwrap(_win.edit_state(), [&](auto & tx) {
                tx.set_sample(selection[0].top());
            });
        }
    }

    void on_right_click(QPoint const& pos) {
        auto index = _list->indexAt(pos);
        std::optional<SampleIndex> sample_idx;
        if (index.isValid()) {
            release_assert((size_t) index.row() < doc::MAX_SAMPLES);
            sample_idx = (SampleIndex) index.row();
        }

        auto const& samples = document().samples;

        auto menu = new QMenu(_list);
        menu->setAttribute(Qt::WA_DeleteOnClose);

        auto add = menu->addAction(tr("&Add Sample"));
        connect(
            add, &QAction::triggered,
            this, index.isValid()
                ? &SampleListImpl::on_add
                : &SampleListImpl::add_at_begin);

        if (sample_idx && samples[*sample_idx].has_value()) {
            {
                auto remove = menu->addAction(tr("&Remove Sample"));
                connect(
                    remove, &QAction::triggered,
                    this, &SampleListImpl::on_remove);
            }
            {
                auto clone = menu->addAction(tr("&Clone Sample"));
                connect(
                    clone, &QAction::triggered,
                    this, &SampleListImpl::on_clone);
            }
            menu->addSeparator();
            {
                auto edit = menu->addAction(tr("&Edit..."));
                connect(
                    edit, &QAction::triggered,
                    this, &SampleListImpl::on_edit_sample);
            }
        }

        menu->popup(_list->viewport()->mapToGlobal(pos));
    }

    void on_edit_sample() {
        if (document().samples[curr_sample_idx()].has_value()) {
            _win.show_sample_dialog();
        }
    }

    void on_add() {
        // If empty slots are visible, allow initializing samples in empty slots
        // through the toolbar, instead of only through the right-click menu.
        add_sample(_show_empty_slots ? curr_sample_idx() : 0);
    }

    void add_sample(SampleIndex begin_idx) {
        using edit::edit_sample_list::try_add_sample;

        auto [maybe_edit, new_sample] = try_add_sample(document(), begin_idx);
        if (!maybe_edit) {
            return;
        }

        auto tx = _win.edit_unwrap();
        tx.push_edit(std::move(maybe_edit), IGNORE_CURSOR);
        tx.set_sample(new_sample);
    }

    void add_at_begin() {
        add_sample(0);
    }

    void on_remove() {
        using edit::edit_sample_list::try_remove_sample;

        auto [maybe_edit, new_sample] =
            try_remove_sample(document(), curr_sample_idx());
        if (!maybe_edit) {
            return;
        }

        auto tx = _win.edit_unwrap();
        tx.push_edit(std::move(maybe_edit), IGNORE_CURSOR);
        tx.sample_deleted();

        // If empty slots are hidden, removing an sample hides it from the list.
        // To keep the cursor in place, move the cursor to the next visible sample.
        if (!_show_empty_slots) {
            tx.set_sample(new_sample);
        }
    }

    void on_clone() {
        clone_sample(_show_empty_slots ? curr_sample_idx() : 0);
    }

    void clone_sample(SampleIndex begin_idx) {
        using edit::edit_sample_list::try_clone_sample;

        auto [maybe_edit, new_sample] =
            try_clone_sample(document(), curr_sample_idx(), begin_idx);
        if (!maybe_edit) {
            return;
        }

        auto tx = _win.edit_unwrap();
        tx.push_edit(std::move(maybe_edit), IGNORE_CURSOR);
        tx.set_sample(new_sample);
    }

    void on_show_empty(bool show) {
        _show_empty_slots = show;
        recompute_visible_slots();
    }

    void on_rename(QString const& qname) {
        using edit::edit_sample_list::try_rename_sample;

        auto maybe_edit =
            try_rename_sample(document(), curr_sample_idx(), qname.toStdString());
        if (!maybe_edit) {
            return;
        }

        auto tx = _win.edit_unwrap();
        tx.push_edit(std::move(maybe_edit), IGNORE_CURSOR);
    }
};
W_OBJECT_IMPL(SampleListImpl)
}

SampleList * SampleList::make(MainWindow * win, QWidget * parent) {
    return new SampleListImpl(win, parent);
}

// namespace
}
