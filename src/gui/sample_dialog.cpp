#include "sample_dialog.h"
#include "doc.h"
#include "gui/lib/dpi.h"
#include "gui/lib/format.h"
#include "gui/lib/icon_toolbar.h"
#include "gui/lib/layout_macros.h"
#include "gui/lib/list_warnings.h"
#include "gui/lib/small_button.h"
#include "util/unwrap.h"
#include "edit/edit_sample_list.h"

#include <verdigris/wobjectimpl.h>

// Widgets
#include <QGroupBox>
#include <QLabel>
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

#include <algorithm>  // std::max
#include <utility>

namespace gui::sample_dialog {
W_OBJECT_IMPL(SampleDialog)

using doc::SampleIndex;
using gui::lib::dpi::dpi_scale;
using namespace gui::lib::list_warnings;
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

    std::vector<QString> _sample_warnings;
    QIcon _warning_icon;
    QColor _warning_color;

// impl
public:
    SampleListModel(MainWindow * win)
        : _win(win)
        , _warning_icon(warning_icon())
        , _warning_color(warning_bg())
    {
        _sample_warnings.resize(doc::MAX_SAMPLES);
    }

    [[nodiscard]] doc::Document const & get_document() const {
        return _win->state().document();
    }

    void reload_state() {
        // TODO move the call to beginResetModel() to a signal emitted when
        // StateTransaction::history_mut() is first called.
        beginResetModel();

        doc::Document const& doc = get_document();

        for (size_t sample_idx = 0; sample_idx < doc::MAX_SAMPLES; sample_idx++) {
            auto const& sample = doc.samples[sample_idx];
            if (!sample.has_value()) {
                _sample_warnings[sample_idx] = QString();
                continue;
            }

            std::vector<QString> all_warnings;

            if (sample->brr.empty()) {
                all_warnings.push_back(tr("Sample is empty"));
            }

            _sample_warnings[sample_idx] = warning_tooltip(all_warnings);
        }

        endResetModel();
    }

    bool has_warning(size_t row) const {
        return !_sample_warnings[row].isEmpty();
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

        case Qt::DecorationRole:
            if (has_warning(row)) {
                return _warning_icon;
            } else {
                return QVariant();
            }

        case Qt::ToolTipRole:
            return _sample_warnings[row];

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

class ColumnListView : public QListView {
public:
    ColumnListView(QWidget * parent = nullptr)
        : QListView(parent)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

        // Tie width to viewportSizeHint() (smaller, scales with font size).
        setSizeAdjustPolicy(QListView::AdjustToContents);
    }

protected:
    QSize viewportSizeHint() const override {
        const int w = qMax(4, fontMetrics().averageCharWidth());
        return QSize(20 * w, 192);
    }
};

using gui::lib::small_button::small_button;

using main_window::StateComponent;

class SampleDialogImpl final : public SampleDialog {
    W_OBJECT(SampleDialogImpl)
public:
    MainWindow & _win;
    SampleListModel _model;
    SampleIndex _curr_sample = 0;
    bool _show_empty_slots = false;

    // Widgets
    QToolButton * _import;
    QToolButton * _remove;
    QToolButton * _clone;
    // TODO add export/import buttons
    QToolButton * _show_empty;
    QListView * _list;

    QWidget * _sample_panel;
    QLineEdit * _rename;

    explicit SampleDialogImpl(MainWindow * win, QWidget * parent)
        : SampleDialog(parent)
        , _win(*win)
        , _model(win)
    {
        setAttribute(Qt::WA_DeleteOnClose);

        // Hide contextual-help button in the title bar.
        setWindowFlags(windowFlags().setFlag(Qt::WindowContextHelpButtonHint, false));

        build_ui();
        connect_ui();
        reload_state();
    }

    void build_ui() {
        auto c = this;
        auto l = new QHBoxLayout(c);
        setLayout(l);

        {l__c_l(QGroupBox(tr("All samples")), QVBoxLayout);
            {l__l(QHBoxLayout);
                {l__w_factory(small_button("+"));
                    _import = w;
                }
                {l__w_factory(small_button("x"));
                    _remove = w;
                }
                {l__w_factory(small_button("C"));
                    _clone = w;
                }
                {l__w_factory(small_button("_"));
                    _show_empty = w;
                    w->setCheckable(true);
                }
                append_stretch();
            }
            {l__w(ColumnListView);
                _list = w;
            }
        }

        {l__c_l(QWidget, QVBoxLayout, 1);
            _sample_panel = c;

            {l__w(QLineEdit);
                _rename = w;
            }
            {l__w(QLabel("\nTODO add sample graph\n"));
                w->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
                w->setStyleSheet("border: 1px solid black;");
                w->setAlignment(Qt::AlignCenter);
            }
        }
    }

    void connect_ui() {
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
            this, &SampleDialogImpl::on_selection_changed);

        // Enable right-click menus for sample list.
        _list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(
            _list, &QWidget::customContextMenuRequested,
            this, &SampleDialogImpl::on_right_click);

        // Connect toolbar.
        connect(
            _import, &QToolButton::clicked,
            this, &SampleDialogImpl::on_import);
        connect(
            _remove, &QToolButton::clicked,
            this, &SampleDialogImpl::on_remove);
        connect(
            _clone, &QToolButton::clicked,
            this, &SampleDialogImpl::on_clone);
        connect(
            _show_empty, &QToolButton::toggled,
            this, &SampleDialogImpl::on_show_empty);

        connect(
            _rename, &QLineEdit::textEdited,
            this, &SampleDialogImpl::on_rename);
    }

    doc::Document const& document() const {
        return _win.state().document();
    }

    /// Returned index may not exist, and may be hidden in the list view.
    SampleIndex curr_sample_idx() {
        return _curr_sample;
    }

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
        _clone->setEnabled(sample.has_value());
        _rename->setEnabled(sample.has_value());
        _sample_panel->setEnabled(sample.has_value());

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
            int row = selection[0].top();
            release_assert(0 <= row && (size_t) row < doc::MAX_SAMPLES);
            _curr_sample = (SampleIndex) row;
            update_selection();
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

        auto add = menu->addAction(tr("&Import Sample"));
        connect(
            add, &QAction::triggered,
            this, index.isValid()
                ? &SampleDialogImpl::on_import
                : &SampleDialogImpl::import_at_begin);

        if (sample_idx && samples[*sample_idx].has_value()) {
            {
                auto remove = menu->addAction(tr("&Remove Sample"));
                connect(
                    remove, &QAction::triggered,
                    this, &SampleDialogImpl::on_remove);
            }
            {
                auto clone = menu->addAction(tr("&Clone Sample"));
                connect(
                    clone, &QAction::triggered,
                    this, &SampleDialogImpl::on_clone);
            }
        }

        menu->popup(_list->viewport()->mapToGlobal(pos));
    }

    void on_import() {
        // If empty slots are visible, allow initializing instruments in empty slots
        // through the toolbar, instead of only through the right-click menu.
        import_sample(_show_empty_slots ? _curr_sample : 0);
    }

    void import_sample(SampleIndex begin_idx) {
        // TODO open dialog
        using edit::edit_sample_list::try_add_sample;

        auto [maybe_edit, new_sample] = try_add_sample(document(), begin_idx);
        if (!maybe_edit) {
            return;
        }

        auto tx = _win.edit_unwrap();
        tx.push_edit(std::move(maybe_edit), IGNORE_CURSOR);
        _curr_sample = new_sample;
    }

    void import_at_begin() {
        import_sample(0);
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
        // Don't close sample dialog when deleting sample.

        // If empty slots are hidden, removing an sample hides it from the list.
        // To keep the cursor in place, move the cursor to the next visible sample.
        if (!_show_empty_slots) {
            _curr_sample = new_sample;
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
        _curr_sample = new_sample;
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
W_OBJECT_IMPL(SampleDialogImpl)
}

SampleDialog * SampleDialog::make(MainWindow * win, QWidget * parent) {
    return new SampleDialogImpl(win, parent);
}

// namespace
}
