#include "sample_dialog.h"
#include "doc.h"
#include "gui/lib/format.h"
#include "gui/lib/layout_macros.h"
#include "gui/lib/list_warnings.h"
#include "gui/lib/small_button.h"
#include "edit/edit_sample.h"
#include "edit/edit_sample_list.h"
#include "util/defer.h"

#include <verdigris/wobjectimpl.h>

// Widgets
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>

// Layouts
#include <QFormLayout>
#include <QVBoxLayout>

// Other
#include <QAbstractListModel>
#include <QAction>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QSignalBlocker>

#include <algorithm>  // std::max
#include <cstring>  // memcpy
#include <utility>

namespace gui::sample_dialog {
W_OBJECT_IMPL(SampleDialog)

using doc::SampleIndex;
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
                tx.set_sample_index((SampleIndex) replace_row);
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
        return QSize(24 * w, 192);
    }
};

using gui::lib::small_button::small_button;

using main_window::StateComponent;

static QSpinBox * wide_spinbox(QWidget * parent = nullptr) {
    auto out = new QSpinBox(parent);
    out->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    return out;
}

static void set_value(QSpinBox * spin, int value) {
    auto b = QSignalBlocker(spin);
    spin->setValue(value);
}

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
    // TODO add export/WAV import buttons
    QToolButton * _show_empty;
    QListView * _list;

    QWidget * _sample_panel;
    QLineEdit * _rename;
    QSpinBox * _loop_point;
    QSpinBox * _sample_rate;
    QSpinBox * _root_key;
    QSpinBox * _detune;

    bool _editing_loop_point = false;

    explicit SampleDialogImpl(SampleIndex sample, MainWindow * win, QWidget * parent)
        : SampleDialog(parent)
        , _win(*win)
        , _model(win)
    {
        build_ui();
        connect_ui();
        reload_state(sample);
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

            {l__l(QHBoxLayout);
                {l__form(QFormLayout);
                    {form__label_wptr(tr("Loop point"), wide_spinbox());
                        _loop_point = w;
                        w->setSingleStep(16);
                    }
                    {form__label_wptr(tr("Sample rate"), wide_spinbox());
                        _sample_rate = w;
                        w->setMinimum(doc::MIN_SAMPLE_RATE);
                        w->setMaximum(doc::MAX_SAMPLE_RATE);
                    }
                    // TODO make NoteSpinBox independent of InstrumentDialogImpl
                    // and use it?
                    {form__label_wptr(tr("Root key"), wide_spinbox());
                        _root_key = w;
                        w->setMaximum(doc::CHROMATIC_COUNT - 1);
                    }
                    {form__label_wptr(tr("Detune"), wide_spinbox());
                        _detune = w;
                        w->setMinimum(-100);
                        w->setMaximum(100);
                    }
                }
                append_stretch(1);
            }
            append_stretch(1);
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
            _list->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, &SampleDialogImpl::on_row_changed);

        // Enable right-click menus for sample list.
        _list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(
            _list, &QWidget::customContextMenuRequested,
            this, &SampleDialogImpl::on_right_click);

        // Connect toolbar.
        connect(
            _import, &QToolButton::clicked,
            this, &SampleDialogImpl::on_import_sample);
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

        // Connect right panel.
        // Our editing functions will crash if the current sample is missing.
        // When the current sample is missing, all spinboxes are disabled
        // so we'll never trigger the crash (hopefully).

        auto connect_spin = [this](QSpinBox * spin, auto func) {
            connect(
                spin, qOverload<int>(&QSpinBox::valueChanged),
                this, func);
        };

        // When the user edits the loop point (setting the loop byte to
        // sample / 16 * 9), the GUI spinbox skips updating
        // (since the user may be in the middle of typing).
        // So when the user finishes editing the spinbox, update it to (byte / 9 * 16).
        connect_spin(_loop_point, &SampleDialogImpl::loop_point_changed);
        connect(
            _loop_point, &QSpinBox::editingFinished,
            this, &SampleDialogImpl::reload_current_sample);

        connect_spin(_sample_rate, &SampleDialogImpl::sample_rate_changed);
        connect_spin(_root_key, &SampleDialogImpl::root_key_changed);
        connect_spin(_detune, &SampleDialogImpl::detune_changed);
    }

    doc::Document const& document() const {
        return _win.state().document();
    }

    /// Returned index may not exist, and may be hidden in the list view.
    SampleIndex curr_sample_idx() {
        return _curr_sample;
    }

    void reload_state(std::optional<SampleIndex> sample) override {
        _model.reload_state();
        recompute_visible_slots();
        if (sample) {
            _curr_sample = *sample;
        }
        reload_current_sample();
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

    void reload_current_sample() {
        auto sample_idx = curr_sample_idx();
        auto const& maybe_sample = document().samples[sample_idx];
        bool valid_sample = maybe_sample.has_value();

        // Update window title.
        if (maybe_sample) {
            setWindowTitle(
                tr("Sample %1 - %2").arg(
                    format_hex_2(sample_idx),
                    QString::fromStdString(maybe_sample->name))
            );
        } else {
            setWindowTitle(tr("Sample %1").arg(format_hex_2(sample_idx)));
        }

        // Update sample list selection.
        auto idx = _model.index((int) sample_idx, 0);
        {
            QItemSelectionModel & list_select = *_list->selectionModel();
            // _list->selectionModel() merely responds to the active sample.
            // Block signals when we change it to match the active sample.
            auto b = QSignalBlocker(list_select);
            list_select.setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
        }

        // Hack to avoid scrolling a widget before it's shown
        // (which causes broken layout and crashes).
        if (isVisible()) {
            _list->scrollTo(idx);
        }

        // Update sample editor.
        doc::Sample fallback{};
        auto const& sample = maybe_sample ? *maybe_sample : fallback;

        _remove->setEnabled(valid_sample);
        _clone->setEnabled(valid_sample);
        _rename->setEnabled(valid_sample);
        _sample_panel->setEnabled(valid_sample);

        // Update control values.
        {
            auto b = QSignalBlocker(_rename);
            auto name = QString::fromStdString(sample.name);
            if (_rename->text() != name) {
                _rename->setText(std::move(name));
            }
        }

        {
            auto b = QSignalBlocker(_loop_point);

            // The maximum loop point is the beginning of the last full block,
            // or 0 if no full blocks are present.
            // (Partial blocks can't be imported, but are ignored if present anyway.)

            auto num_blocks = int(sample.brr.size() / 9);
            auto last_block = std::max(num_blocks - 1, 0);
            _loop_point->setMaximum(last_block * 16);
        }

        if (!_editing_loop_point) {
            set_value(_loop_point, int(sample.loop_byte) / 9 * 16);
        }
        set_value(_sample_rate, (int) sample.tuning.sample_rate);
        set_value(_root_key, sample.tuning.root_key);
        set_value(_detune, sample.tuning.detune_cents);
    }

    void on_row_changed(QModelIndex const& current) {
        int sample = current.row();
        release_assert(0 <= sample && (size_t) sample < doc::MAX_SAMPLES);
        _curr_sample = (SampleIndex) sample;
        reload_current_sample();
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

        if (index.isValid()) {
            auto add = menu->addAction(tr("&Replace Sample"));
            connect(
                add, &QAction::triggered,
                this, &SampleDialogImpl::on_replace_sample);
        } else {
            auto add = menu->addAction(tr("&Import Sample"));
            connect(
                add, &QAction::triggered,
                this, &SampleDialogImpl::on_import_sample);
        }

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

    void on_replace_sample() {
        import_sample(_curr_sample);
    }

    void on_import_sample() {
        import_sample({});
    }

    /// Import a sample.
    /// If a sample number is supplied, it is replaced (and tuning is preserved).
    /// Otherwise a sample is inserted into the first empty slot
    /// (or next to the cursor if empty slots are shown, for consistency with
    /// cloning samples or adding/cloning instruments... don't ask why).
    void import_sample(std::optional<SampleIndex> sample_idx) {
        using edit::edit_sample_list::replace_sample;
        using edit::edit_sample_list::try_add_sample;

        // TODO remember recent folder in options
        auto path = QFileDialog::getOpenFileName(
            this, tr("Import Sample"), "", tr("BRR samples (*.brr);;All files (*)")
        );
        if (path.isEmpty()) {
            return;
        }

        auto file = QFile(path);
        if (!file.open(QFile::ReadOnly)) {
            QMessageBox::critical(
                this,
                tr("Sample import error"),
                tr("Failed to open file: %1").arg(file.errorString()));
            return;
        }

        qint64 size = file.size();
        bool valid = (size >= 9 && size <= 0x10000);
        bool no_header = valid && size % 9 == 0;
        bool has_header = valid && size % 9 == 2;
        valid = valid && (no_header || has_header);

        if (!valid) {
            QMessageBox::critical(
                this,
                tr("Sample import error"),
                tr("Invalid file size (%1 bytes), must be multiple of 9 (with optional 2-byte header)")
                    .arg(size));
            return;
        }

        auto const data = file.readAll();
        if (data.size() != size) {
            QMessageBox::critical(
                this,
                tr("Sample import error"),
                tr("Failed to read file data, expected %1 bytes, read %2 bytes, error %3")
                    .arg(size)
                    .arg(data.size())
                    .arg(file.errorString()));
            return;
        }

        doc::Document const& doc = document();
        doc::Sample sample;

        // Preserve metadata if replacing an existing sample.
        if (sample_idx && doc.samples[*sample_idx]) {
            sample = *doc.samples[*sample_idx];
        } else {
            sample = doc::Sample {
                .name = "",
                .brr = {},
                .loop_byte = 0,
                .tuning = doc::SampleTuning {
                    .sample_rate = 16000,
                    .root_key = 60,
                    .detune_cents = 0,
                },
            };
        }

        // Unconditionally overwrite name.
        sample.name = QFileInfo(path).baseName().toStdString();

        // Overwrite sample data, and if header is present, overwrite loop point.
        if (has_header) {
            // This assumes the CPU is little-endian.
            memcpy(&sample.loop_byte, data.begin(), 2);
            sample.brr = std::vector<uint8_t>(data.begin() + 2, data.end());
        } else {
            sample.brr = std::vector<uint8_t>(data.begin(), data.end());
        }

        edit::EditBox edit;
        SampleIndex new_idx;
        if (sample_idx) {
            edit = replace_sample(doc, *sample_idx, std::move(sample));
            new_idx = *sample_idx;
        } else {
            SampleIndex search_idx = _show_empty_slots ? curr_sample_idx() : 0;
            auto [maybe_edit, new_idx_] =
                try_add_sample(doc, search_idx, std::move(sample));
            if (!maybe_edit) {
                return;
            }
            edit = std::move(maybe_edit);
            new_idx = new_idx_;
        }

        auto tx = _win.edit_unwrap();
        tx.push_edit(std::move(edit), IGNORE_CURSOR);
        _curr_sample = new_idx;
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

    void loop_point_changed(int loop_smp) {
        using edit::edit_sample::set_loop_byte;

        // Floor-divide the sample count by 16 to ensure an integer block count.
        // The casts are messy, but I don't particularly care what happens to
        // out-of-bounds values.
        auto loop_byte = (uint16_t) (uint32_t(loop_smp) / 16 * 9);

        _editing_loop_point = true;
        defer {
            _editing_loop_point = false;
        };
        {
            auto tx = _win.edit_unwrap();
            tx.push_edit(
                set_loop_byte(document(), curr_sample_idx(), loop_byte), IGNORE_CURSOR
            );
        }
    }

    void sample_rate_changed(int sample_rate) {
        using edit::edit_sample::set_sample_rate;
        auto tx = _win.edit_unwrap();
        tx.push_edit(
            set_sample_rate(document(), curr_sample_idx(), (uint32_t) sample_rate),
            IGNORE_CURSOR);
    }

    void root_key_changed(int root_key) {
        using edit::edit_sample::set_root_key;
        auto tx = _win.edit_unwrap();
        tx.push_edit(
            set_root_key(document(), curr_sample_idx(), (doc::Chromatic) root_key),
            IGNORE_CURSOR);
    }

    void detune_changed(int detune) {
        using edit::edit_sample::set_detune_cents;
        auto tx = _win.edit_unwrap();
        tx.push_edit(
            set_detune_cents(document(), curr_sample_idx(), (int16_t) detune),
            IGNORE_CURSOR);
    }
};
W_OBJECT_IMPL(SampleDialogImpl)
}

SampleDialog * SampleDialog::make(
    SampleIndex sample, MainWindow * win, QWidget * parent
) {
    return new SampleDialogImpl(sample, win, parent);
}

// namespace
}
