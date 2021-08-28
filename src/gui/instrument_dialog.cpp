#include "instrument_dialog.h"
#include "instrument_dialog/adsr_graph.h"
#include "gui_common.h"
#include "gui/lib/format.h"
#include "gui/lib/parse_note.h"
#include "gui/lib/layout_macros.h"
#include "gui/lib/docs_palette.h"
#include "edit/edit_instr.h"
#include "util/defer.h"
#include "util/release_assert.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QToolButton>

#include <QBoxLayout>
#include <QGridLayout>

#include <QDebug>
#include <QEvent>
#include <QProxyStyle>
#include <QScreen>
#include <QSignalBlocker>
#include <QStyleHints>
#include <QWheelEvent>

#include <utility>  // std::move
#include <variant>

namespace gui::instrument_dialog {

class ListWidget : public QListWidget {
    QSize viewportSizeHint() const {
        const int w = qMax(4, fontMetrics().averageCharWidth());
        return QSize(20 * w, 0);
    }
};

using gui::lib::parse_note::ParseIntState;

class InstrumentDialogImpl;
class NoteSpinBox final : public QSpinBox {
    // We cannot use parent(), because placing NoteSpinBox in a widget
    // within a InstrumentDialogImpl means the NoteSpinBox's parent
    // is no longer a InstrumentDialogImpl but some other QWidget.
    // We *can* use window(), but that is risky.
    InstrumentDialogImpl * _dlg;

    mutable bool _show_longest_str = false;
    mutable QString _prev_text;
    mutable ParseIntState _prev_state{};

public:
    explicit NoteSpinBox(InstrumentDialogImpl * parent);

// impl QSpinBox
protected:
    QString textFromValue(int value) const override;
    QValidator::State validate(QString &text, int &pos) const override;
    int valueFromText(const QString &text) const override;

    static inline const QLatin1String LONGEST_STR = QLatin1String("C#-1");

    QSize sizeHint() const override {
        _show_longest_str = true;
        defer { _show_longest_str = false; };
        return QSpinBox::sizeHint();
    }

    QSize minimumSizeHint() const override {
        _show_longest_str = true;
        defer { _show_longest_str = false; };
        return QSpinBox::minimumSizeHint();
    }
};

class SmallSpinBox final : public QSpinBox {
    /// The longest possible value this widget can display without overflowing.
    int _longest_value;
    mutable bool _show_longest_value = false;

    /// The size of the longest possible value this widget is expected to display.
    /// Used as the minimum on-screen size of this NumericViewer.
    QSize _minimum_size;

    bool _inverted = false;

public:
    SmallSpinBox(int longest_value, QWidget * parent = nullptr)
        : QSpinBox(parent)
        , _longest_value(longest_value)
    {
        // Removing buttons should reduce widget width,
        // but fails to do so on KDE Breeze 5.22.4 and below.
        setButtonSymbols(QSpinBox::NoButtons);
    }

    void set_inverted(bool invert) {
        _inverted = invert;
    }

// impl QWidget
public:
    QSize sizeHint() const override {
        _show_longest_value = true;
        defer { _show_longest_value = false; };
        return QSpinBox::sizeHint();
    }

    QSize minimumSizeHint() const override {
        _show_longest_value = true;
        defer { _show_longest_value = false; };
        return QSpinBox::minimumSizeHint();
    }

// override QSpinBox
protected:
    QString textFromValue(int value) const override {
        // It's OK (for now) to return different values during sizeHint(),
        // because Q[Abstract]SpinBox doesn't cache textFromValue()'s return value...
        // yay fragile base classes
        if (_show_longest_value) {
            return QSpinBox::textFromValue(_longest_value);
        } else {
            return QSpinBox::textFromValue(value);
        }
    }

    StepEnabled stepEnabled() const override {
        StepEnabled orig = QSpinBox::stepEnabled();
        if (_inverted) {
            StepEnabled out = StepNone;
            if (orig & StepUpEnabled) {
                out |= StepDownEnabled;
            }
            if (orig & StepDownEnabled) {
                out |= StepUpEnabled;
            }
            return out;
        } else {
            return orig;
        }
    }

public:
    void stepBy(int step) override {
        return QSpinBox::stepBy(_inverted ? -step : step);
    }
};

QToolButton * small_button(const QString &text, QWidget *parent = nullptr) {
    auto w = new QToolButton(parent);
    w->setText(text);
    return w;
}

/// Make the slider jump to the point of click,
/// instead of stepping up/down by increments.
class SliderSnapStyle : public QProxyStyle {
public:
    // Do not pass a borrowed QStyle* to the QProxyStyle constructor.
    // QProxyStyle takes ownership of the QStyle and automatically deletes it.
    // Instead don't pass an argument at all. This makes it use the app style.
    SliderSnapStyle()
        // Ensure a consistent appearance across platforms, for recoloring sliders.
        : QProxyStyle("fusion")
    {}

    int styleHint(
        QStyle::StyleHint hint,
        const QStyleOption* option = 0,
        const QWidget* widget = 0,
        QStyleHintReturn* returnData = 0) const override
    {
        if (hint == QStyle::SH_Slider_AbsoluteSetButtons)
            return Qt::LeftButton;
        if (hint == QStyle::SH_Slider_PageSetButtons)
            return Qt::MiddleButton | Qt::RightButton;
        if (hint == QStyle::SH_Slider_SloppyKeyEvents)
            return true;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

namespace pal = gui::lib::docs_palette;
using pal::Shade;

class AdsrSlider : public QSlider {
    QPalette _orig_palette;
    pal::Hue _hue;
    bool _hovered = false;

public:
    explicit AdsrSlider(
        SliderSnapStyle * style, pal::Hue hue, QWidget * parent = nullptr
    )
        : QSlider(Qt::Vertical, parent)
        , _orig_palette(palette())
        , _hue(hue)
    {
        setStyle(style);
        update_color();
    }

private:
    void update_color() {
        if (!isEnabled()) {
            setPalette(_orig_palette);
            return;
        }

        QPalette p = _orig_palette;

        QColor fg_and_groove, active_groove;
        if (!_hovered) {
            fg_and_groove = pal::get_color(_hue, 6, 1.5);
        } else {
            fg_and_groove = pal::get_color(_hue, 5.25);
        }
        active_groove = pal::get_color(_hue, 4.5);

        p.setColor(QPalette::Button, fg_and_groove);
        p.setColor(QPalette::Highlight, active_groove);
        setPalette(p);
    }

// impl QWidget
public:
    QSize sizeHint() const override {
        // Note that QSlider::sizeHint() does not scale with DPI.
        auto size = QSlider::sizeHint();
        // devicePixelRatio() is always 1.
        qreal dpi_scale = logicalDpiY() / qreal(96);

        size.setWidth(std::max(size.width(), int(20 * dpi_scale)));
        size.setHeight(std::max(size.height(), int(80 * dpi_scale)));
        return size;
    }

    QSize minimumSizeHint() const override {
        return QSlider::sizeHint();
    }

protected:
    void changeEvent(QEvent * event) override {
        if (event->type() == QEvent::EnabledChange) {
            update_color();
        }
    }

    void enterEvent(QEvent * event) override {
        if (event->type() == QEvent::Enter) {
            _hovered = true;
            update_color();
        }
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent * event) override {
        if (event->type() == QEvent::Leave) {
            _hovered = false;
            update_color();
        }
        QWidget::leaveEvent(event);
    }

// override QSlider
protected:
    void wheelEvent(QWheelEvent * e) override {
        QStyleHints * sh = QApplication::styleHints();

        // Block QStyleHints::wheelScrollLinesChanged().
        auto b = QSignalBlocker(sh);

        // Set QApplication::wheelScrollLines(),
        // which controls "steps per click" for QAbstractSlider,
        // not just "lines per click" for scrollable regions.
        int lines = sh->wheelScrollLines();
        defer { sh->setWheelScrollLines(lines); };
        sh->setWheelScrollLines(2);

        // Scroll by 2 lines at a time.
        return QSlider::wheelEvent(e);
    }
};

/// On KDE Plasma's Breeze theme, this prevents dragging the *window body*
/// from moving the window like dragging the title bar.
class NoDragContainer : public QWidget {
public:
    // NoDragContainer()
    using QWidget::QWidget;

// impl QWidget
    void mousePressEvent(QMouseEvent *event) override {
        event->accept();
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

struct Control {
    AdsrSlider * slider;
    SmallSpinBox * number;

    void set_value(int value) {
        auto bs = QSignalBlocker(slider);
        auto bn = QSignalBlocker(number);
        slider->setValue(value);
        number->setValue(value);
    }
};

template<typename Label>
struct LabeledControl {
    Label * label;
    AdsrSlider * slider;
    SmallSpinBox * number;

    Control no_label() {
        return Control{slider, number};
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
    SliderSnapStyle _slider_snap;

    // widgets
    QToolButton * _add_patch;
    QToolButton * _remove_patch;
    QToolButton * _move_patch_up;
    QToolButton * _move_patch_down;
    QListWidget * _keysplit;
    QCheckBox * _note_names;

    QWidget * _patch_panel;
    QSpinBox * _min_key;
    QComboBox * _sample;
    Control _attack;
    Control _decay;
    Control _sustain;
    Control _decay2;
    QCheckBox * _release_enable;
    Control _release;

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
        using doc::Adsr;

        // TODO add tabs
        {l__c_l(QWidget, QVBoxLayout, 1);
            _patch_panel = c;
            l->setContentsMargins(0, 0, 0, 0);
            // Top row.
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

            // Bottom.
            {l__l(QHBoxLayout);
                // Keysplit editor.
                // NoDragContainer is used so if you try to drag a slider but drag
                // the background instead, KDE/Breeze won't move the dialog.
                {l__c_l(NoDragContainer, QGridLayout, 0, Qt::AlignVCenter);
                    l->setContentsMargins(0, 0, 0, -1);

                    // Make grid tighter on Breeze. dpi switching? lolnope
//                    l->setVerticalSpacing(6);
                    l->setHorizontalSpacing(6);

                    namespace colors = adsr_graph::colors;

                    int column = 0;
                    _attack = build_control(l, column,
                        qlabel(tr("AR")), colors::ATTACK, Adsr::MAX_ATTACK_RATE
                    ).no_label();
                    _decay = build_control(l, column,
                        qlabel(tr("DR")), colors::DECAY, Adsr::MAX_DECAY_RATE
                    ).no_label();
                    _sustain = build_control(l, column,
                        qlabel(tr("SL")), colors::SUSTAIN, Adsr::MAX_SUSTAIN_LEVEL
                    ).no_label();
                    _decay2 = build_control(l, column,
                        qlabel(tr("D2")), colors::DECAY2, Adsr::MAX_DECAY_2
                    ).no_label();

                    // TODO add exponential release GAIN
                    // (used for note cuts, not note changes)
                    {
                        auto release = build_control(l, column,
                            new QCheckBox(tr("R")), colors::RELEASE, Adsr::MAX_DECAY_2
                        );
                        release.label->setDisabled(true);
                        release.slider->setDisabled(true);
                        release.number->setDisabled(true);

                        _release_enable = release.label;
                        _release = release.no_label();
                    }

                    // Invert the slider of a "rate" control
                    // to make it act as a duration control.
                    auto invert = [](Control & ctrl) {
                        ctrl.slider->setInvertedAppearance(true);
                        ctrl.slider->setInvertedControls(true);
                        ctrl.number->set_inverted(true);
                    };
                    invert(_attack);
                    invert(_decay);
                    invert(_decay2);
                    invert(_release);

                    // Switch tab order so you can tab from one slider to the next,
                    // then from one spinbox to the next. I find it more intuitive.
                    tab_by_row(*l);
                }

                // ADSR graph.
                {l__c_l(QFrame, QVBoxLayout);
                    c->setFrameStyle(int(QFrame::StyledPanel) | QFrame::Sunken);
                    l->setContentsMargins(0, 0, 0, 0);
                    {l__w(AdsrGraph);
                        _adsr_graph = w;
                    }
                }
            }
        }
    }

    template<typename Label>
    LabeledControl<Label> build_control(
        QGridLayout * l, int & column, Label * label, pal::Hue color, int max
    ) {
        AdsrSlider * slider;
        SmallSpinBox * text;
        {l__w_factory(label, 0, column, Qt::AlignHCenter);
            w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        }
        {l__w(AdsrSlider(&_slider_snap, color), 1, column);
            slider = w;
            w->setMaximum(max);
            w->setPageStep((max + 1) / 4);
            w->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
        }
        {l__w(SmallSpinBox(99), 2, column, Qt::AlignHCenter);
            text = w;
            w->setMaximum(max);
            w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        }
        column++;
        return LabeledControl<Label> { label, slider, text };
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
            _keysplit->setCurrentRow((int) new_patch_idx);
        }
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
                _keysplit->setCurrentRow((int) patch_idx);
            }
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
                _keysplit->setCurrentRow((int) (patch_idx - 1));
            }
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
                _keysplit->setCurrentRow((int) (patch_idx + 1));
            }
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
        auto connect_pair = [&](Control pair, auto make_edit) {
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

            auto text = QString("%1: %2")
                .arg(format_note_name(patch.min_note), name);
            // TODO for single-key drum patch, print "=%1: %2"

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
        if (!instr->keysplit.empty()) {
            assert(patch_idx < instr->keysplit.size());
        }

        // out-of-bounds patch_idx should only happen in blank instruments,
        // which should either be prohibited or treated as a no-op.
        if (patch_idx < instr->keysplit.size()) {
            patch = instr->keysplit[patch_idx];
            _patch_panel->setDisabled(false);
        } else {
            _patch_panel->setDisabled(true);
        }

        set_value(_min_key, patch.min_note);

        reload_samples(_sample, doc, patch);

        _attack.set_value(patch.adsr.attack_rate);
        _decay.set_value(patch.adsr.decay_rate);
        _sustain.set_value(patch.adsr.sustain_level);
        _decay2.set_value(patch.adsr.decay_2);

        _adsr_graph->set_adsr(patch.adsr);
    }
};

NoteSpinBox::NoteSpinBox(InstrumentDialogImpl * parent)
    : QSpinBox(parent)
    , _dlg(parent)
{}

QString NoteSpinBox::textFromValue(int value) const {
    // It's OK (for now) to return different values during sizeHint(),
    // because Q[Abstract]SpinBox doesn't cache textFromValue()'s return value...
    // yay fragile base classes
    if (_show_longest_str) {
        return LONGEST_STR;
    }

    return _dlg->format_note_name((doc::Chromatic) value);
}

using gui::lib::parse_note::parse_note_name;

QValidator::State NoteSpinBox::validate(QString & text, int & pos) const  {
    if (_prev_text == text && !text.isEmpty()) {
        return _prev_state.state;
    }

    _prev_text = text;
    _prev_state = parse_note_name(get_app().options().note_names, text, pos);
    return _prev_state.state;
}

int NoteSpinBox::valueFromText(const QString & text) const {
    if (_prev_text == text && !text.isEmpty()) {
        return _prev_state.value;
    }

    QString copy = text;
    int pos = lineEdit()->cursorPosition();
    _prev_text = copy;
    _prev_state = parse_note_name(get_app().options().note_names, copy, pos);
    return _prev_state.value;
}

InstrumentDialog * InstrumentDialog::make(MainWindow * parent_win) {
    return new InstrumentDialogImpl(parent_win);
}

} // namespace
