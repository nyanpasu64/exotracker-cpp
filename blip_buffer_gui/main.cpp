#include "gui/lib/layout_macros.h"

#include <Blip_Buffer/Blip_Buffer.h>

#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QSlider>
#include <QFormLayout>
#include <QCheckBox>
#include <qwt/qwt_plot.h>
#include <qwt/qwt_plot_grid.h>
#include <qwt/qwt_plot_curve.h>
#include <qwt/qwt_scale_engine.h>

#define MOVE
#define BORROW
using std::move;

class BlipViewerWindow : public QWidget {
    QwtPlot * _plot;
    QwtPlotGrid * _grid;
    QwtPlotCurve * _curve;

    QCheckBox * _log_scale;
    QLabel * _width_nsamp_label;
    QLabel * _sample_rate_label;
    QLabel * _cutoff_freq_label;
    QLabel * _treble_freq_label;
    QLabel * _treble_db_label;

    QSlider * _width_nsamp;
    QSlider * _sample_rate;
    QSlider * _cutoff_freq;
    QSlider * _treble_freq;
    QSlider * _treble_db;

    bool _draw_queued = false;

public:
    using Self = BlipViewerWindow;

    BlipViewerWindow(QWidget * parent = nullptr) : QWidget(parent) {
        // GUI setup
        auto c = this;

        auto l = new QVBoxLayout;
        c->setLayout(l);

        {l__w(QwtPlot);
            _plot = w;
            w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        }

        // Object setup
        _curve = new QwtPlotCurve;
        MOVE _curve->attach(_plot);

        _grid = new QwtPlotGrid;
        MOVE _grid->attach(_plot);

        {l__c_form(QWidget, QFormLayout);
            #define ASSIGN(NAME) \
                NAME##_label = left; \
                NAME = right; \
                auto w = right;

            {form__label_w("", QCheckBox("Log Scale"));
                _log_scale = w;
            }

            {form__left_right(QLabel, QSlider);
                ASSIGN(_width_nsamp)
                w->setOrientation(Qt::Horizontal);
                w->setRange(8, 128);
                w->setValue(64);
            }
            {form__left_right(QLabel, QSlider);
                ASSIGN(_sample_rate)
                w->setOrientation(Qt::Horizontal);
                w->setRange(0, 96000);
                w->setValue(44100);
                w->setSingleStep(100);
                w->setPageStep(1000);
            }
            {form__left_right(QLabel, QSlider);
                ASSIGN(_cutoff_freq)
                w->setOrientation(Qt::Horizontal);
                w->setRange(0, 48000);
                w->setValue(23100);  // 0 = "pick automatically"
                w->setSingleStep(100);
                w->setPageStep(1000);
            }
            {form__left_right(QLabel, QSlider);
                ASSIGN(_treble_freq)
                w->setOrientation(Qt::Horizontal);
                w->setRange(0, 48000);
                w->setValue(21000);
                w->setSingleStep(100);
                w->setPageStep(1000);
            }
            {form__left_right(QLabel, QSlider);
                ASSIGN(_treble_db)
                w->setOrientation(Qt::Horizontal);
                w->setRange(-90, 5);
                w->setValue(-24);  // famitracker's default value... an empty blip_eq_t() defaults to 0.
            }
        }

        QObject::connect(_log_scale, &QCheckBox::toggled, this, &Self::draw);
        QObject::connect(_width_nsamp, &QSlider::valueChanged, this, &Self::draw);
        QObject::connect(_sample_rate, &QSlider::valueChanged, this, &Self::draw);
        QObject::connect(_cutoff_freq, &QSlider::valueChanged, this, &Self::draw);
        QObject::connect(_treble_freq, &QSlider::valueChanged, this, &Self::draw);
        QObject::connect(_treble_db, &QSlider::valueChanged, this, &Self::draw);
        draw();
    }

    void force_draw() {
        #define LABEL(NAME, TEMPLATE) \
            NAME##_label->setText(QStringLiteral(TEMPLATE).arg(NAME->value()))

        LABEL(_width_nsamp, "Full-width (samples): %1");
        LABEL(_sample_rate, "Sample rate (Hz): %1");
        LABEL(_cutoff_freq, "Cutoff frequency (Hz): %1");
        LABEL(_treble_freq, "Treble shelf (Hz): %1");
        LABEL(_treble_db, "Treble shelf (dB): %1");

        _draw_queued = false;

        // Generate impulse
        int width = _width_nsamp->value();
        auto eq = blip_eq_t(
            _treble_db->value(), _treble_freq->value(), _sample_rate->value(), _cutoff_freq->value()
        );

        std::vector<float> fimpulse;
        fimpulse.resize(blip_res / 2 * width + blip_res);

        int const half_size = blip_res / 2 * (width - 1);
        eq.generate( &fimpulse [blip_res], half_size );

        // Plot data
        bool log_scale = _log_scale->isChecked();
        if (log_scale) {
            _plot->setAxisScale(QwtPlot::yLeft, -96, 0);

        } else {
            _plot->setAxisScale(QwtPlot::yLeft, -0.25, 1);
        }

        QVector<double> xs, ys;
        auto points = QList<QPointF>();
        for (int i = 0; i < blip_res + half_size; i++) {
            double x = i - (blip_res + half_size);
            x /= blip_res;
            xs.append(x);

            double y = fimpulse[i] / 4096;
            if (log_scale) {
                ys.append(20. * log10(fabs(y)));
            } else {
                ys.append(y);
            }
        }

        _curve->setSamples(xs, ys);
        _plot->replot();
    };

    void draw() {
        if (!_draw_queued) {
            _draw_queued = true;
            QMetaObject::invokeMethod(this, &Self::force_draw, Qt::QueuedConnection);
        }
    };
};

int main(int argc, char *argv[]) {
    auto app = QApplication(argc, argv);

    auto w = BlipViewerWindow();

    w.resize(800, 600);
    w.show();
    return app.exec();
}