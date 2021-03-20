#include "gui/lib/layout_macros.h"

#include <Blip_Buffer/Blip_Buffer.h>

#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QSlider>
#include <QFormLayout>
#include <qwt/qwt_plot.h>
#include <qwt/qwt_plot_grid.h>
#include <qwt/qwt_plot_curve.h>

#define MOVE
#define BORROW
using std::move;

class BlipViewerWindow : public QWidget {
    QwtPlot * _plot;
    QwtPlotGrid * _grid;
    QwtPlotCurve * _curve;

    QLabel * _width_nsamp_label;
    QLabel * _treble_db_label;
    QLabel * _rolloff_freq_label;
    QLabel * _sample_rate_label;
    QLabel * _cutoff_freq_label;

    QSlider * _width_nsamp;
    QSlider * _treble_db;
    QSlider * _rolloff_freq;
    QSlider * _sample_rate;
    QSlider * _cutoff_freq;

    bool _draw_queued = false;

public:
    using Self = BlipViewerWindow;

    BlipViewerWindow(QWidget * parent = nullptr) : QWidget(parent) {
//        _series = new QLineSeries();
//        _chart->legend()->hide();
//        _chart->addSeries(MOVE _series);
//        _chart->setTitle("blip_buffer");

//        _axisX = new QValueAxis;
//        _axisX->setTickAnchor(0);
//        _axisX->setTickInterval(0.5);
//        _axisX->setTickType(QValueAxis::TicksDynamic);
//        _axisX->setLabelFormat("%.1f");
//        _chart->addAxis(MOVE _axisX, Qt::AlignBottom);
//        _series->attachAxis(_axisX);

//        _axisY = new QValueAxis;
//        _axisY->setTickAnchor(0);
//        _axisY->setTickInterval(2048);
//        _axisY->setTickType(QValueAxis::TicksDynamic);
//        _axisY->setLabelFormat("%.0f");
//        _chart->addAxis(MOVE _axisY, Qt::AlignLeft);
//        _series->attachAxis(_axisY);

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
        _curve->attach(_plot);

        _grid = new QwtPlotGrid;
        _grid->attach(_plot);

        {l__c_form(QWidget, QFormLayout);
            #define ASSIGN(NAME) \
                NAME##_label = left; \
                NAME = right; \
                auto w = right;

            {form__left_right(QLabel, QSlider);
                ASSIGN(_width_nsamp)
                w->setOrientation(Qt::Horizontal);
                w->setRange(8, 32);
                w->setValue(16);
            }
            {form__left_right(QLabel, QSlider);
                ASSIGN(_treble_db)
                w->setOrientation(Qt::Horizontal);
                w->setRange(-90, 5);
                w->setValue(-24);  // famitracker's default value... an empty blip_eq_t() defaults to 0.
            }
            {form__left_right(QLabel, QSlider);
                ASSIGN(_rolloff_freq)
                w->setOrientation(Qt::Horizontal);
                w->setRange(0, 48000);
                w->setValue(12000);  // famitracker's default value... an empty blip_eq_t() defaults to 0.
                w->setSingleStep(100);
                w->setPageStep(1000);
            }
            {form__left_right(QLabel, QSlider);
                ASSIGN(_sample_rate)
                w->setOrientation(Qt::Horizontal);
                w->setRange(0, 96000);
                w->setValue(48000);
                w->setSingleStep(100);
                w->setPageStep(1000);
            }
            {form__left_right(QLabel, QSlider);
                ASSIGN(_cutoff_freq)
                w->setOrientation(Qt::Horizontal);
                w->setRange(0, 48000);
                w->setValue(0);  // idk what this does, famitracker doesn't supply it, defaults to 0.
                w->setSingleStep(100);
                w->setPageStep(1000);
            }
        }

        QObject::connect(_width_nsamp, &QSlider::valueChanged, this, &Self::draw);
        QObject::connect(_treble_db, &QSlider::valueChanged, this, &Self::draw);
        QObject::connect(_rolloff_freq, &QSlider::valueChanged, this, &Self::draw);
        QObject::connect(_sample_rate, &QSlider::valueChanged, this, &Self::draw);
        QObject::connect(_cutoff_freq, &QSlider::valueChanged, this, &Self::draw);
        draw();
    }

    void force_draw() {
        #define LABEL(NAME, TEMPLATE) \
            NAME##_label->setText(QStringLiteral(TEMPLATE).arg(NAME->value()))

        LABEL(_width_nsamp, "Half-width (samples): %1");
        LABEL(_treble_db, "Treble (Nyquist?) attenuation (dB): %1");
        LABEL(_rolloff_freq, "Treble attenuation frequency: %1");
        LABEL(_sample_rate, "Sample rate: %1");
        LABEL(_cutoff_freq, "Cutoff frequency (?): %1");

        _draw_queued = false;
        int width = _width_nsamp->value();
        auto eq = blip_eq_t(
            _treble_db->value(), _rolloff_freq->value(), _sample_rate->value(), _cutoff_freq->value()
        );

        std::vector<float> fimpulse;
        fimpulse.resize(blip_res / 2 * width + blip_res);

        int const half_size = blip_res / 2 * (width - 1);
        eq.generate( &fimpulse [blip_res], half_size );

        QVector<double> xs, ys;
        auto points = QList<QPointF>();
        for (int i = 0; i < blip_res + half_size; i++) {
            double x = i - (blip_res + half_size);
            x /= blip_res;
            xs.append(x);
            ys.append(fimpulse[i]);
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
