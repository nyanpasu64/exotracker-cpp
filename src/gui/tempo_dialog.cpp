#include "tempo_dialog.h"
#include "lib/layout_macros.h"
#include "lib/hv_line.h"
#include "doc.h"
#include "audio/tempo_calc.h"
#include "edit/edit_doc.h"

#include <verdigris/wobjectimpl.h>

// widgets
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

// layouts
#include <QBoxLayout>
#include <QGridLayout>
#include <QFormLayout>

// misc
#include <QEvent>
#include <QFontMetrics>

#include <limits>  // std::numeric_limits<>
#include <utility>  // std::move

namespace gui::tempo_dialog {

namespace {

class IntFormatter {
    QString _suffix;

public:
    void set_suffix(QString suffix) {
        _suffix = std::move(suffix);
    }

    QString format(QLocale locale, int value) const {
        // based on QSpinBox::textFromValue().

        locale.setNumberOptions(QLocale::OmitGroupSeparator);
        QString str = locale.toString(value);
        str += _suffix;
        return str;
    }
};

class DoubleFormatter {
    QString _suffix;
    int _decimals = 2;

public:
    void set_suffix(QString suffix) {
        _suffix = std::move(suffix);
    }

    void set_decimals(int decimals) {
        _decimals = decimals;
    }

    QString format(QLocale locale, double value) const {
        // based on QDoubleSpinBox::textFromValue().

        locale.setNumberOptions(QLocale::OmitGroupSeparator);
        QString str = locale.toString(value, 'f', _decimals);
        str += _suffix;
        return str;
    }
};

template<typename T, typename Formatter>
class NumericViewer : public QLabel {
    /// Turns a T into a QString to use as this NumericViewer's text.
    Formatter _text_formatter;

    /// The size of the longest possible value this widget is expected to display.
    /// Used as the minimum on-screen size of this NumericViewer.
    QSize _minimum_size;

    /// The longest possible value this widget can display without overflowing.
    /// Fed into _text_formatter to compute _size_hint.
    T _longest_value;

    /// The currently shown value, fed into _text_formatter
    /// and used as this NumericViewer's text.
    T _current_value;

public:
    explicit NumericViewer(T longest_value, QWidget *parent = nullptr)
        : QLabel(parent)
        , _longest_value(longest_value)
        , _current_value(longest_value)  // stub value, should be overwritten before being read
    {
        reload_size_hint();

        // Make text selectable (why not?)
        setTextInteractionFlags(Qt::TextSelectableByMouse);
        setCursor(QCursor(Qt::IBeamCursor));

        // Make widget grow to fit available space.
        // Reduces the chance of text overflowing the widget
        // (but won't help with the largest widget).
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

private:
    void reload_size_hint() {
        _minimum_size =
            fontMetrics().size(0, _text_formatter.format(locale(), _longest_value));
    }

    void reload_text() {
        setText(_text_formatter.format(locale(), _current_value));
    }

public:
    void setValue(T val) {
        _current_value = val;
        reload_text();
    }

    /// Only valid to call on DoubleViewer.
    void setDecimals(int prec) {
        _text_formatter.set_decimals(prec);
        reload_size_hint();
        reload_text();
    }

    void setSuffix(const QString &suffix) {
        _text_formatter.set_suffix(suffix);
        reload_size_hint();
        reload_text();
    }

// impl QWidget
public:
    QSize sizeHint() const override {
        return _minimum_size.expandedTo(QLabel::sizeHint());
    }

    QSize minimumSizeHint() const override {
        return _minimum_size.expandedTo(QLabel::minimumSizeHint());
    }

protected:
    /// Recompute sizeHint() when font settings change.
    void changeEvent(QEvent * event) override {
        // Copied from QAbstractSpinBox::event(...), hopefully this is enough.
        switch (event->type()) {
        case QEvent::FontChange:
        case QEvent::StyleChange:
        // case QEvent::LocaleChange?
        // QAbstractSpinBox::event(...) doesn't recompute the size hint (or text)
        // on QEvent::LocaleChange, but QSpinBox::textFromValue(...) "depends" on locale.
        // However I think it's *technically* not a bug for Q[Double]SpinBox:
        //
        // - I don't know if changing the locale of a running app or login session
        //   is possible on any OS.
        //
        // - The size hint of a Q[Double]SpinBox depends on
        //   Q[Double]SpinBox::textFromValue(), which uses locale.toString(...)
        //   with OmitGroupSeparator enabled.
        //
        // - QLocale::toString(qlonglong i) with OmitGroupSeparator calls
        //   QLocaleData::longLongToString(...) with flags of 0.
        //   This calls qulltoa() (locale-independent) and looks at flags.
        //   Since flags == 0, and QLocaleData::longLongToString
        //   doesn't seem to look at member variables,
        //   the output is independent of the current locale.
        //
        // - QLocale::toString(double f, 'c') manages to escape locale-dependence
        //   but only because of format 'c'.
        //
        // I think QDateTimeEdit is wrong to not recompute on locale change,
        // but luckily we don't have a DateTimeViewer so it doesn't affect us ;)

            reload_size_hint();
            break;
        default:
            break;
        }
        QLabel::changeEvent(event);
    }
};

using IntViewer = NumericViewer<int, IntFormatter>;
using DoubleViewer = NumericViewer<double, DoubleFormatter>;

class TempoDialogImpl : public TempoDialog {
    W_OBJECT(TempoDialogImpl)

    GetDocument _get_document;
    doc::SequencerOptions _options;

    // User-editable parameters.
    QDoubleSpinBox * _target_beats_per_min;
    QSpinBox * _spc_timer_period;
    QSpinBox * _ticks_per_beat;

    // Read-only outputs.
    IntViewer * _engine_tempo;
    DoubleViewer * _actual_beats_per_min;
    DoubleViewer * _timers_per_s;
    DoubleViewer * _ms_per_timer;
    DoubleViewer * _bpm_step;

    // Show/hide right side of dialog.
    QCheckBox * _show_advanced;
    QWidget * _advanced_widget;

    // Buttons
    QPushButton * _ok;
    QPushButton * _apply;
    QPushButton * _cancel;

public:
    TempoDialogImpl(GetDocument get_document, MainWindow * parent)
        : TempoDialog(parent)
        , _get_document(get_document)
        , _options(_get_document.get_document().sequencer_options)
    {
        setWindowTitle(tr("Tempo Settings"));
        // prevent leaking dialogs.
        setAttribute(Qt::WA_DeleteOnClose);

        // Hide contextual-help button in the title bar.
        // None of our widgets have help or tooltips.
        setWindowFlags(windowFlags().setFlag(Qt::WindowContextHelpButtonHint, false));

        // What to do about _options vs. global document?
        // (global document is only updated upon Apply, not in real-time.)

        auto l = new QHBoxLayout();
        setLayout(l);
        l->setSizeConstraint(QLayout::SetFixedSize);

        // Add extra gap between form label and widget, for breathing room.
        // TODO scale with DPI?
        // (wish Qt let you mix virtual and physical pixels through a tagged union)
        constexpr int HORIZONTAL_SPACING = 8;

        {l__l(QVBoxLayout);
            {l__c_l(QGroupBox(tr("Basic")), QVBoxLayout);
                {l__form(QFormLayout);
                    form->setHorizontalSpacing(HORIZONTAL_SPACING);
                    {form__label_w(tr("Target tempo"), QDoubleSpinBox);
                        _target_beats_per_min = w;
                        w->setRange(doc::MIN_TEMPO, doc::MAX_TEMPO);
                        w->setValue(_options.target_tempo);
                        w->setSuffix(tr(" BPM"));
                    }
                }
                {l__w(HLine);
                }
                {l__form(QFormLayout);
                    form->setHorizontalSpacing(HORIZONTAL_SPACING);
                    {form__label_w(tr("Engine tempo:"), IntViewer(255));
                        _engine_tempo = w;
                    }
                    {form__label_w(tr("Actual tempo:"), DoubleViewer(9999.));
                        _actual_beats_per_min = w;
                        w->setSuffix(tr(" BPM"));
                    }
                }
            }
            {l__w(QCheckBox(tr("Show advanced options")));
                _show_advanced = w;
                // TODO fetch state from app options
            }
            {l__c_l(QGroupBox(tr("Advanced")), QVBoxLayout);
                _advanced_widget = c;

                {l__form(QFormLayout);
                    form->setHorizontalSpacing(HORIZONTAL_SPACING);
                    {form__label_w(tr("Timer register"), QSpinBox);
                        _spc_timer_period = w;
                        w->setRange(doc::MIN_TIMER_PERIOD, doc::MAX_TIMER_PERIOD);
                        w->setValue((int) _options.spc_timer_period);
                    }
                    {form__label_w(tr("Ticks/beat"), QSpinBox);
                        _ticks_per_beat = w;
                        w->setRange(doc::MIN_TICKS_PER_BEAT, doc::MAX_TICKS_PER_BEAT);
                        w->setValue((int) _options.ticks_per_beat);
                    }
                }
                {l__w(HLine);
                }
                {l__form(QFormLayout);
                    form->setHorizontalSpacing(HORIZONTAL_SPACING);
                    {form__label_w(tr("Timer frequency:"), DoubleViewer(9999.));
                        _timers_per_s = w;
                        w->setSuffix(tr(" Hz"));
                    }
                    {form__label_w(tr("Period (note jitter):"), DoubleViewer(99.));
                        _ms_per_timer = w;
                        w->setDecimals(3);
                        w->setSuffix(tr(" ms"));
                    }
                    {form__label_w(tr("Tempo step:"), DoubleViewer(99.));
                        _bpm_step = w;
                        w->setDecimals(3);
                        w->setSuffix(tr(" BPM"));
                    }
                }
            }
        }

        {l__w(QDialogButtonBox);
            w->setOrientation(Qt::Vertical);

            // By default, QDialogButtonBox's layout differs between OSes.
            // The default vertical layout on non-Windows OSes is bad
            // because the cancel button is located at the bottom
            // and moves when the dialog is expanded/contracted.
            // The easiest way to pick a custom layout is through stylesheets.
            static_assert(QDialogButtonBox::WinLayout == 0);
            w->setStyleSheet("button-layout: 0;");

            _ok = w->addButton(QDialogButtonBox::Ok);
            _cancel = w->addButton(QDialogButtonBox::Cancel);
            _apply = w->addButton(QDialogButtonBox::Apply);
        }

        update_state();

        connect(
            _show_advanced, &QCheckBox::toggled,
            this, &TempoDialogImpl::update_state);

        auto connect_spin = [this](QSpinBox * spin) {
            connect(
                spin, qOverload<int>(&QSpinBox::valueChanged),
                this, &TempoDialogImpl::update_state);
        };
        auto connect_dspin = [this](QDoubleSpinBox * spin) {
            connect(
                spin, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, &TempoDialogImpl::update_state);
        };
        connect_dspin(_target_beats_per_min);
        connect_spin(_spc_timer_period);
        connect_spin(_ticks_per_beat);

        connect(
            _ok, &QPushButton::clicked,
            this, [this]() {
                save_document();
                accept();
            });
        connect(_apply, &QPushButton::clicked, this, &TempoDialogImpl::save_document);
        connect(_cancel, &QPushButton::clicked, this, &TempoDialogImpl::reject);
    }

    /// Recompute the output widgets showing the rounded tempo.
    void update_state() {
        namespace tempo = audio::tempo_calc;
        using tempo::calc_sequencer_rate;
        using tempo::calc_clocks_per_timer;

        // TODO save state to app options
        bool checked = _show_advanced->isChecked();

        // TODO hide _engine_tempo?
        _advanced_widget->setVisible(checked);

        // Calculate tempo values.
        _options.target_tempo = _target_beats_per_min->value();
        _options.spc_timer_period = (uint32_t) _spc_timer_period->value();
        _options.ticks_per_beat = (uint32_t) _ticks_per_beat->value();

        uint8_t sequencer_rate = calc_sequencer_rate(_options);

        double timers_per_s =
            double(tempo::CLOCKS_PER_S_IDEAL)
            / double(calc_clocks_per_timer(_options.spc_timer_period));

        auto calc_bpm = [this, timers_per_s](int sequencer_rate) {
            constexpr double s_per_min = 60.;

            double ticks_per_s = timers_per_s * double(sequencer_rate) / 256.;
            double beats_per_s = ticks_per_s / double(_options.ticks_per_beat);
            double beats_per_min = beats_per_s * s_per_min;
            return beats_per_min;
        };

        double beats_per_min = calc_bpm(sequencer_rate);
        double bpm_step = calc_bpm(1);

        constexpr double ms_per_s = 1000.;

        _engine_tempo->setValue((int) sequencer_rate);
        _actual_beats_per_min->setValue(beats_per_min);
        _timers_per_s->setValue(timers_per_s);
        _ms_per_timer->setValue(ms_per_s / timers_per_s);
        _bpm_step->setValue(bpm_step);
    }

    void save_document() {
        using edit::edit_doc::set_sequencer_options;
        using main_window::keep_cursor;

        auto const& doc = _get_document.get_document();

        auto tx = win().edit_unwrap();
        win().push_edit(tx, set_sequencer_options(doc, _options), keep_cursor());
    }

    MainWindow & win() {
        auto main_window = qobject_cast<MainWindow *>(parent());
        assert(main_window);
        return *main_window;
    }
};
W_OBJECT_IMPL(TempoDialogImpl)
}  // anonymous namespace

W_OBJECT_IMPL(TempoDialog)

TempoDialog * TempoDialog::make(GetDocument get_document, MainWindow * parent) {
    return new TempoDialogImpl(get_document, parent);
}

}
