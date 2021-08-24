#include "instrument_dialog/adsr_graph.h"
#include "instrument_dialog.h"
#include "gui_common.h"
#include "gui/lib/format.h"
#include "gui/lib/layout_macros.h"
#include "edit/edit_instr.h"
#include "util/release_assert.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QSpinBox>
#include <QtWidgets/private/qabstractspinbox_p.h>
#include <QToolButton>

#include <QBoxLayout>
#include <QGridLayout>

#include <QDebug>
#include <QEvent>
#include <QScreen>
#include <QSignalBlocker>

#include <utility>  // std::move

namespace gui::instrument_dialog {

class ListWidget : public QListWidget {
    QSize viewportSizeHint() const {
        const int w = qMax(4, fontMetrics().averageCharWidth());
        return QSize(20 * w, 0);
    }
};

#define D_FUNC(Class) \
    inline Class##Private* d_func() \
    { Q_CAST_IGNORE_ALIGN(return reinterpret_cast<Class##Private *>(qGetPtrHelper(d_ptr));) } \
    inline const Class##Private* d_func() const \
    { Q_CAST_IGNORE_ALIGN(return reinterpret_cast<const Class##Private *>(qGetPtrHelper(d_ptr));) }

class InstrumentDialogImpl;
class NoteSpinBox final : public QSpinBox {
public:
    explicit NoteSpinBox(InstrumentDialogImpl * parent);

private:
    D_FUNC(QAbstractSpinBox)

    InstrumentDialogImpl const& dlg() const;

protected:
    QString textFromValue(int value) const override;

    static inline const QLatin1String LONGEST_STR = QLatin1String("C#-1");

    QSize sizeHint() const override {
        Q_D(const QAbstractSpinBox);
        if (d->cachedSizeHint.isEmpty()) {
            ensurePolished();

            const QFontMetrics fm(fontMetrics());
            int h = d->edit->sizeHint().height();
            int w = 0;
            QString s = LONGEST_STR;
            QString fixedContent =  d->prefix + d->suffix + QLatin1Char(' ');
            s += fixedContent;
            w = qMax(w, fm.horizontalAdvance(s));

            if (d->specialValueText.size()) {
                s = d->specialValueText;
                w = qMax(w, fm.horizontalAdvance(s));
            }
            w += 2; // cursor blinking space

            QStyleOptionSpinBox opt;
            initStyleOption(&opt);
            QSize hint(w, h);
            d->cachedSizeHint = style()->sizeFromContents(QStyle::CT_SpinBox, &opt, hint, this);
        }
        return d->cachedSizeHint;
    }

    QSize minimumSizeHint() const override {
        Q_D(const QAbstractSpinBox);
        if (d->cachedMinimumSizeHint.isEmpty()) {
            //Use the prefix and range to calculate the minimumSizeHint
            ensurePolished();

            const QFontMetrics fm(fontMetrics());
            int h = d->edit->minimumSizeHint().height();
            int w = 0;

            QString s = LONGEST_STR;
            QString fixedContent =  d->prefix + QLatin1Char(' ');
            s += fixedContent;
            w = qMax(w, fm.horizontalAdvance(s));

            if (d->specialValueText.size()) {
                s = d->specialValueText;
                w = qMax(w, fm.horizontalAdvance(s));
            }
            w += 2; // cursor blinking space

            QStyleOptionSpinBox opt;
            initStyleOption(&opt);
            QSize hint(w, h);

            d->cachedMinimumSizeHint = style()->sizeFromContents(QStyle::CT_SpinBox, &opt, hint, this);
        }
        return d->cachedMinimumSizeHint;
    }
};

class NumericEditor final : public QSpinBox {
    /// The longest possible value this widget can display without overflowing.
    int _longest_value;

    /// The size of the longest possible value this widget is expected to display.
    /// Used as the minimum on-screen size of this NumericViewer.
    QSize _minimum_size;

public:
    NumericEditor(int longest_value, QWidget * parent = nullptr)
        : QSpinBox(parent)
        , _longest_value(longest_value)
    {
        // Removing buttons should reduce widget width,
        // but fails to do so on KDE Breeze 5.22.4 and below.
        setButtonSymbols(QSpinBox::NoButtons);
    }

private:
    D_FUNC(QAbstractSpinBox)

// impl QWidget
public:
    // please don't change QAbstractSpinBoxPrivate in kde/5.15
    QSize sizeHint() const override {
        Q_D(const QAbstractSpinBox);
        if (d->cachedSizeHint.isEmpty()) {
            ensurePolished();

            const QFontMetrics fm(fontMetrics());
            int h = d->edit->sizeHint().height();
            int w = 0;
            QString s;
            QString fixedContent =  d->prefix + d->suffix + QLatin1Char(' ');
            s = d->textFromValue(_longest_value);
            s.truncate(18);
            s += fixedContent;
            w = qMax(w, fm.horizontalAdvance(s));

            if (d->specialValueText.size()) {
                s = d->specialValueText;
                w = qMax(w, fm.horizontalAdvance(s));
            }
            w += 2; // cursor blinking space

            QStyleOptionSpinBox opt;
            initStyleOption(&opt);
            QSize hint(w, h);
            d->cachedSizeHint = style()->sizeFromContents(QStyle::CT_SpinBox, &opt, hint, this);
        }
        return d->cachedSizeHint;
    }

    QSize minimumSizeHint() const override {
        Q_D(const QAbstractSpinBox);
        if (d->cachedMinimumSizeHint.isEmpty()) {
            //Use the prefix and range to calculate the minimumSizeHint
            ensurePolished();

            const QFontMetrics fm(fontMetrics());
            int h = d->edit->minimumSizeHint().height();
            int w = 0;

            QString s;
            QString fixedContent =  d->prefix + QLatin1Char(' ');
            s = d->textFromValue(_longest_value);
            s.truncate(18);
            s += fixedContent;
            w = qMax(w, fm.horizontalAdvance(s));

            if (d->specialValueText.size()) {
                s = d->specialValueText;
                w = qMax(w, fm.horizontalAdvance(s));
            }
            w += 2; // cursor blinking space

            QStyleOptionSpinBox opt;
            initStyleOption(&opt);
            QSize hint(w, h);

            d->cachedMinimumSizeHint = style()->sizeFromContents(QStyle::CT_SpinBox, &opt, hint, this);
        }
        return d->cachedMinimumSizeHint;
    }
};

QToolButton * small_button(const QString &text, QWidget *parent = nullptr) {
    auto w = new QToolButton(parent);
    w->setText(text);
    return w;
}

class TallSlider : public QSlider {
public:
    explicit TallSlider(QWidget * parent = nullptr)
        : QSlider(Qt::Vertical, parent)
    {}

// impl QWidget
    QSize sizeHint() const override {
        // Note that QSlider::sizeHint() does not scale with DPI.
        auto size = QSlider::sizeHint();
        // screen()->devicePixelRatio() is always 1.
        qreal dpi_scale = screen()->logicalDotsPerInch() / qreal(96);

        size.setHeight(std::max(size.height(), int(80 * dpi_scale)));
        return size;
    }

    QSize minimumSizeHint() const override {
        return QSlider::sizeHint();
    }
};

static int current_row(QListWidget const& view) {
    int selection = view.currentRow();
    if (selection == -1) {
        selection = 0;
    }
    return selection;
}

using gui::lib::format::format_hex_2;
using gui::lib::format::format_note_keysplit;

static QString sample_text(doc::Samples const& samples, size_t sample_idx) {
    assert(sample_idx < samples.v.size());
    auto const& maybe_sample = samples[sample_idx];
    if (maybe_sample) {
        QString name = QString::fromStdString(maybe_sample->name);
        return QLatin1String("%1 - %2").arg(format_hex_2(sample_idx), name);
    } else {
        return QLatin1String("%1").arg(format_hex_2(sample_idx));
    }
}

/// Create a QLabel with a fixed horizontal width.
static QLabel * qlabel(QString text) {
    auto w = new QLabel(std::move(text));
    w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    return w;
}

static void set_value(QSpinBox * spin, int value) {
    auto b = QSignalBlocker(spin);
    spin->setValue(value);
}

static void reload_samples(
    QComboBox * list, doc::Document const& doc, doc::InstrumentPatch const& patch
) {
    auto b = QSignalBlocker(list);

    list->clear();
    for (size_t sample_idx = 0; sample_idx < doc::MAX_SAMPLES; sample_idx++) {
        list->addItem(sample_text(doc.samples, sample_idx));
    }
    list->setCurrentIndex(patch.sample_idx);
}

static void tab_by_row(QGridLayout & l) {
    QWidget * prev = nullptr;
    int nrow = l.rowCount();
    int ncol = l.columnCount();
    for (int r = 0; r < nrow; r++) {
        for (int c = 0; c < ncol; c++) {
            if (auto item = l.itemAtPosition(r, c)) {
                if (auto w = item->widget()) {
                    if (prev) {
                        QWidget::setTabOrder(prev, w);
                    }
                    prev = w;
                }
            }
        }
    }
}

struct Slider {
    QSlider * slider;
    NumericEditor * number;

    void set_value(int value) {
        auto bs = QSignalBlocker(slider);
        auto bn = QSignalBlocker(number);
        slider->setValue(value);
        number->setValue(value);
    }
};

template<typename Label>
struct LabeledSlider {
    Label * label;
    QSlider * slider;
    NumericEditor * number;

    Slider no_label() {
        return Slider{slider, number};
    }
};

template<typename Source>
class Narrow {
    Source _v;

public:
    Narrow(Source v)
        : _v(v)
    {}

    template<typename Target>
    operator Target() const {
        return static_cast<Target>(_v);
    }
};

namespace edit_instr = edit::edit_instr;
namespace MoveCursor = gui::main_window::MoveCursor_;
using adsr_graph::AdsrGraph;

class InstrumentDialogImpl final : public InstrumentDialog {
    MainWindow * _win;

    // widgets
    QToolButton * _add_patch;
    QToolButton * _remove_patch;
    QToolButton * _move_patch_up;
    QToolButton * _move_patch_down;
    QListWidget * _keysplit;
    QCheckBox * _note_names;

    QSpinBox * _min_key;
    QComboBox * _sample;
    Slider _attack;
    Slider _decay;
    Slider _sustain;
    Slider _decay2;
    QCheckBox * _release_enable;
    Slider _release;

    AdsrGraph * _adsr_graph;

public:
    InstrumentDialogImpl(MainWindow * parent_win)
        : InstrumentDialog(parent_win)
        , _win(parent_win)
    {
        setAttribute(Qt::WA_DeleteOnClose);

        // Hide contextual-help button in the title bar.
        setWindowFlags(windowFlags().setFlag(Qt::WindowContextHelpButtonHint, false));

        build_ui();
        connect_ui();
        reload_state();
    }

    void build_ui() {
        auto l = new QVBoxLayout(this);

        {l__l(QHBoxLayout, 1);
            build_keysplit(l);
            build_patch_editor(l);
        }

        build_piano(l);
    }

    void build_keysplit(QBoxLayout * l) {
        {l__c_l(QGroupBox(tr("Keysplits")), QVBoxLayout);
            {l__l(QHBoxLayout);
                // TODO add icons
                {l__w_factory(small_button("+"));
                    _add_patch = w;
                }
                {l__w_factory(small_button("-"));
                    _remove_patch = w;
                }
                {l__w_factory(small_button("↑"));
                    _move_patch_up  = w;
                }
                {l__w_factory(small_button("↓"));
                    _move_patch_down  = w;
                }
                append_stretch();
            }

            {l__w(ListWidget);
                _keysplit = w;
                w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

                // Make keysplit widget smaller and scale with font size
                w->setSizeAdjustPolicy(QListWidget::AdjustToContents);
            }

            {l__w(QCheckBox(tr("Note names")));
                _note_names = w;
            }
        }
    }

    void build_patch_editor(QBoxLayout * l) {
        // TODO add tabs
        {l__l(QVBoxLayout, 1);
            {l__l(QHBoxLayout);
                {l__w_factory(qlabel(tr("Min Key"))); }
                {l__w(NoteSpinBox(this));
                    _min_key = w;
                    w->setMaximum(doc::CHROMATIC_COUNT - 1);
                }

                {l__w_factory(qlabel(tr("Sample"))); }
                {l__w(QComboBox, 1);
                    _sample = w;
                }
            }

            {l__l(QHBoxLayout);
                {l__c_l(QWidget, QGridLayout, 0, Qt::AlignVCenter);
                    l->setContentsMargins(0, 0, 0, -1);

                    // Make grid tighter on Breeze. dpi switching? lolnope
//                    l->setVerticalSpacing(6);
                    l->setHorizontalSpacing(6);

                    int column = 0;
                    _attack = build_slider(
                        l, column, qlabel(tr("A")), doc::Adsr::MAX_ATTACK_RATE
                    ).no_label();
                    _decay = build_slider(
                        l, column, qlabel(tr("D")), doc::Adsr::MAX_DECAY_RATE
                    ).no_label();
                    _sustain = build_slider(
                        l, column, qlabel(tr("S")), doc::Adsr::MAX_SUSTAIN_LEVEL
                    ).no_label();
                    _decay2 = build_slider(
                        l, column, qlabel(tr("D2")), doc::Adsr::MAX_DECAY_2
                    ).no_label();

                    // TODO add exponential release GAIN
                    // (used for note cuts, not note changes)
                    {
                        auto release = build_slider(
                            l, column, new QCheckBox(tr("R")), doc::Adsr::MAX_DECAY_2
                        );
                        release.label->setDisabled(true);
                        release.slider->setDisabled(true);
                        release.number->setDisabled(true);

                        _release_enable = release.label;
                        _release = release.no_label();
                    }

                    // Switch tab order so you can tab from one slider to the next,
                    // then from one spinbox to the next. I find it more intuitive.
                    tab_by_row(*l);
                }

                {l__w(QLabel("\nTODO add graph\n"), 1);
                    w->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
                    w->setStyleSheet("border: 1px solid black;");
                    w->setAlignment(Qt::AlignCenter);
                }
            }
        }
    }

    template<typename Label>
    static LabeledSlider<Label> build_slider(
        QGridLayout * l, int & column, Label * label, int max
    ) {
        QSlider * slider;
        NumericEditor * text;
        {l__w_factory(label, 0, column, Qt::AlignHCenter);
            w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        }
        {l__w(TallSlider, 1, column, Qt::AlignHCenter);
            slider = w;
            w->setMaximum(max);
            w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
        }
        {l__w(NumericEditor(99), 2, column, Qt::AlignHCenter);
            text = w;
            w->setMaximum(max);
            w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        }
        column++;
        return LabeledSlider<Label> { label, slider, text };
    }

    void build_piano(QBoxLayout * l) {
        {l__w(QLabel("\nTODO add piano\n"));
            w->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
            w->setStyleSheet("border: 1px solid black;");
            w->setAlignment(Qt::AlignCenter);
        }
    }

    doc::Document const& document() const {
        return _win->state().document();
    }

    size_t curr_instr_idx() const {
        return (size_t) _win->state().instrument();
    }

    size_t curr_patch_idx() const {
        return (size_t) current_row(*_keysplit);
    }

    template<typename F>
    void widget_changed(QWidget * widget, int value, F make_edit) {
        auto instr_idx = curr_instr_idx();
        auto const& doc = document();

        if (!doc.instruments[instr_idx]) {
            return;
        }
        if (doc.instruments[instr_idx]->keysplit.empty()) {
            return;
        }

        edit::EditBox cmd =
            make_edit(doc, instr_idx, curr_patch_idx(), Narrow(value));

        auto b = QSignalBlocker(widget);
        auto tx = _win->edit_unwrap();
        _win->push_edit(tx, std::move(cmd), MoveCursor::NotPatternEdit{});
    }

    void on_set_min_key(int value) {
        auto instr_idx = curr_instr_idx();
        auto const& doc = document();

        if (!doc.instruments[instr_idx]) {
            return;
        }
        if (doc.instruments[instr_idx]->keysplit.empty()) {
            return;
        }

        auto [cmd, new_patch_idx] =
            edit_instr::edit_min_key(doc, instr_idx, curr_patch_idx(), Narrow(value));

        {
            auto b = QSignalBlocker(_min_key);
            auto tx = _win->edit_unwrap();
            _win->push_edit(tx, std::move(cmd), MoveCursor::NotPatternEdit{});
        }
        _keysplit->setCurrentRow((int) new_patch_idx);
    }

    void on_add_patch() {
        // if keysplit is empty, currentRow() is -1.
        // auto patch_idx = (size_t) (_keysplit->currentRow() + 1);
        auto patch_idx = (size_t) _keysplit->model()->rowCount();

        if (auto edit = edit_instr::try_add_patch(
            document(), curr_instr_idx(), patch_idx
        )) {
            {
                auto tx = _win->edit_unwrap();
                _win->push_edit(tx, std::move(edit), MoveCursor::NotPatternEdit{});
                // TODO move ~StateTransaction() logic to StateTransaction::commit()
            }
            _keysplit->setCurrentRow((int) patch_idx);
        }
    }

    void on_remove_patch() {
        if (auto edit = edit_instr::try_remove_patch(
            document(), curr_instr_idx(), curr_patch_idx()
        )) {
            auto tx = _win->edit_unwrap();
            _win->push_edit(tx, std::move(edit), MoveCursor::NotPatternEdit{});
            // leave current row unchanged
        }
    }

    void on_move_patch_up() {
        auto patch_idx = curr_patch_idx();
        if (auto edit = edit_instr::try_move_patch_up(
            document(), curr_instr_idx(), patch_idx
        )) {
            {
                auto tx = _win->edit_unwrap();
                _win->push_edit(tx, std::move(edit), MoveCursor::NotPatternEdit{});
            }
            _keysplit->setCurrentRow((int) (patch_idx - 1));
        }
    }

    void on_move_patch_down() {
        auto patch_idx = curr_patch_idx();
        if (auto edit = edit_instr::try_move_patch_down(
            document(), curr_instr_idx(), patch_idx
        )) {
            {
                auto tx = _win->edit_unwrap();
                _win->push_edit(tx, std::move(edit), MoveCursor::NotPatternEdit{});
            }
            _keysplit->setCurrentRow((int) (patch_idx + 1));
        }
    }

    QString format_note_name(doc::Chromatic note) const {
        if (_note_names->isChecked()) {
            auto & note_cfg = get_app().options().note_names;
            auto & doc = document();
            return format_note_keysplit(note_cfg, doc.accidental_mode, note);
        } else {
            return QString::number(note);
        }
    }

    void connect_ui() {
        connect(
            _add_patch, &QToolButton::clicked,
            this, &InstrumentDialogImpl::on_add_patch);
        connect(
            _remove_patch, &QToolButton::clicked,
            this, &InstrumentDialogImpl::on_remove_patch);
        connect(
            _move_patch_up, &QToolButton::clicked,
            this, &InstrumentDialogImpl::on_move_patch_up);
        connect(
            _move_patch_down, &QToolButton::clicked,
            this, &InstrumentDialogImpl::on_move_patch_down);

        connect(
            _note_names, &QCheckBox::stateChanged,
            this, &InstrumentDialogImpl::reload_state);

        auto connect_spin = [this](QSpinBox * spin, auto make_edit) {
            connect(
                spin, qOverload<int>(&QSpinBox::valueChanged),
                this, [this, spin, make_edit](int value) {
                    widget_changed(spin, value, make_edit);
                },
                Qt::UniqueConnection
            );
        };
        auto connect_slider = [this](QSlider * slider, auto make_edit) {
            connect(
                slider, &QSlider::valueChanged,
                this, [this, slider, make_edit](int value) {
                    widget_changed(slider, value, make_edit);
                },
                Qt::UniqueConnection
            );
        };
        auto connect_combo = [this](QComboBox * combo, auto make_edit) {
            connect(
                combo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this, combo, make_edit](int value) {
                    widget_changed(combo, value, make_edit);
                },
                Qt::UniqueConnection
            );
        };
        auto connect_pair = [&](Slider pair, auto make_edit) {
            connect_slider(pair.slider, make_edit);
            connect_spin(pair.number, make_edit);
        };

        connect(
            _keysplit, &QListWidget::currentItemChanged,
            this, &InstrumentDialogImpl::reload_current_patch);

        connect(
            _min_key, qOverload<int>(&QSpinBox::valueChanged),
            this, &InstrumentDialogImpl::on_set_min_key);

        connect_combo(_sample, edit_instr::edit_sample_idx);
        connect_pair(_attack, edit_instr::edit_attack);
        connect_pair(_decay, edit_instr::edit_decay);
        connect_pair(_sustain, edit_instr::edit_sustain);
        connect_pair(_decay2, edit_instr::edit_decay2);
    }

    void reload_state() override {
        auto const& state = _win->state();
        auto const& doc = state.document();

        auto instr_idx = curr_instr_idx();

        assert(doc.instruments.v.size() == doc::MAX_INSTRUMENTS);
        release_assert(instr_idx < doc.instruments.v.size());

        auto const& instr = doc.instruments[instr_idx];
        if (!instr) {
            close();
            return;
        }

        setWindowTitle(
            tr("Instrument %1 - %2").arg(
                format_hex_2(instr_idx), QString::fromStdString(instr->name)
            )
        );

        // TODO keep selection iff instrument id unchanged
        reload_keysplit(*_keysplit, *instr, doc.samples, true);
        reload_current_patch();
    }

    /// does not emit change signals (which would invoke reload_current_patch()).
    /// this should be fine, since when update_keysplit() is called by reload_state(),
    /// reload_state subsequently calls reload_current_patch().
    void reload_keysplit(
        QListWidget & list,
        doc::Instrument const& instr,
        doc::Samples const& samples,
        bool keep_selection)
    {
        auto b = QSignalBlocker(&list);

        // TODO ensure we always have exactly 1 element selected.
        // TODO how to handle 0 keysplits? create a dummy keysplit pointing to sample 0?
        int selection = 0;
        if (keep_selection) {
            selection = current_row(list);
        }
        list.clear();

        auto & keysplit = instr.keysplit;

        size_t n = keysplit.size();
        for (size_t patch_idx = 0; patch_idx < n; patch_idx++) {
            doc::InstrumentPatch const& patch = keysplit[patch_idx];
            QString name = sample_text(samples, patch.sample_idx);

            auto text = QString("%1+: %3")
                .arg(format_note_name(patch.min_note), name);

            new QListWidgetItem(text, &list);
            // TODO compute and show list of errors
            // (eg. missing sample, empty or overshadowed key range...)
        }

        if (n > 0) {
            list.setCurrentRow(std::min(selection, int(n) - 1));
        }
    }

    void reload_current_patch() {
        auto const& state = _win->state();
        auto const& doc = state.document();

        auto instr_idx = curr_instr_idx();
        auto const& instr = doc.instruments[instr_idx];
        if (!instr) {
            close();
            return;
        }

        doc::InstrumentPatch patch;
        patch.adsr = {0, 0, 0, 0};

        auto patch_idx = curr_patch_idx();
        // out-of-bounds patch_idx should only happen in blank instruments,
        // which should either be prohibited or treated as a no-op.
        if (patch_idx < instr->keysplit.size()) {
            patch = instr->keysplit[patch_idx];
        }

        set_value(_min_key, patch.min_note);

        reload_samples(_sample, doc, patch);

        _attack.set_value(patch.adsr.attack_rate);
        _decay.set_value(patch.adsr.decay_rate);
        _sustain.set_value(patch.adsr.sustain_level);
        _decay2.set_value(patch.adsr.decay_2);

    }
};

NoteSpinBox::NoteSpinBox(InstrumentDialogImpl * parent)
    : QSpinBox(parent)
{}

InstrumentDialogImpl const& NoteSpinBox::dlg() const {
    return *static_cast<InstrumentDialogImpl const*>(parent());
}

QString NoteSpinBox::textFromValue(int value) const {
    return dlg().format_note_name((doc::Chromatic) value);
}


InstrumentDialog * InstrumentDialog::make(MainWindow * parent_win) {
    return new InstrumentDialogImpl(parent_win);
}

} // namespace
