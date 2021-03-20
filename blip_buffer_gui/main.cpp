#include "gui/lib/layout_macros.h"

#include <Blip_Buffer/Blip_Buffer.h>

#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QSlider>
#include <QtCharts>
#include <QChartView>

using namespace QtCharts;

#define MOVE
#define BORROW
using std::move;

class BlipViewerWindow : public QWidget {
    QChartView * _chartView;

    QChart * _chart;
    QLineSeries * _series;
    QValueAxis * _axisX;
    QValueAxis * _axisY;

    QSlider * _width_nsamp;
    QSlider * _treble_db;
    QSlider * _rolloff_freq;
    QSlider * _sample_rate;
    QSlider * _cutoff_freq;

    bool _draw_queued = false;

public:
    using Self = BlipViewerWindow;

    BlipViewerWindow(QWidget * parent = nullptr) : QWidget(parent) {
        // Object setup
        _chart = new QChart();

        _series = new QLineSeries();
        _chart->legend()->hide();
        _chart->addSeries(MOVE _series);
        _chart->setTitle("blip_buffer");

        _axisX = new QValueAxis;
        _axisX->setTickAnchor(0);
        _axisX->setTickInterval(0.5);
        _axisX->setTickType(QValueAxis::TicksDynamic);
        _axisX->setLabelFormat("%.1f");
        _chart->addAxis(MOVE _axisX, Qt::AlignBottom);
        _series->attachAxis(_axisX);

        _axisY = new QValueAxis;
        _axisY->setTickAnchor(0);
        _axisY->setTickInterval(2048);
        _axisY->setTickType(QValueAxis::TicksDynamic);
        _axisY->setLabelFormat("%.0f");
        _chart->addAxis(MOVE _axisY, Qt::AlignLeft);
        _series->attachAxis(_axisY);

        // GUI setup
        auto c = this;

        auto l = new QVBoxLayout;
        c->setLayout(l);

        QChartView * chartView;
        {l__w(QChartView(MOVE _chart));
            chartView = w;
            w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            chartView->setRenderHint(QPainter::Antialiasing);
        }

        {l__c_form(QWidget, QFormLayout);
            {form__label_w("Half-width (samples)", QSlider);
                _width_nsamp = w;
                w->setOrientation(Qt::Horizontal);
                w->setRange(8, 32);
                w->setValue(16);
            }
            {form__label_w("Treble (Nyquist?) attenuation (dB)", QSlider);
                _treble_db = w;
                w->setOrientation(Qt::Horizontal);
                w->setRange(-90, 5);
                w->setValue(-24);  // famitracker's default value... an empty blip_eq_t() defaults to 0.
            }
            {form__label_w("Treble attenuation frequency", QSlider);
                _rolloff_freq = w;
                w->setOrientation(Qt::Horizontal);
                w->setRange(0, 48000);
                w->setValue(12000);  // famitracker's default value... an empty blip_eq_t() defaults to 0.
            }
            {form__label_w("Sample rate", QSlider);
                _sample_rate = w;
                w->setOrientation(Qt::Horizontal);
                w->setRange(0, 96000);
                w->setValue(48000);
            }
            {form__label_w("Cutoff frequency (?)", QSlider);
                _cutoff_freq = w;
                w->setOrientation(Qt::Horizontal);
                w->setRange(0, 48000);
                w->setValue(0);  // idk what this does, famitracker doesn't supply it, defaults to 0.
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
        _draw_queued = false;
        int width = _width_nsamp->value();
        auto eq = blip_eq_t(
            _treble_db->value(), _rolloff_freq->value(), _sample_rate->value(), _cutoff_freq->value()
        );

        float fimpulse [blip_res / 2 * (blip_widest_impulse_ - 1) + blip_res * 2] = {0};

        int const half_size = blip_res / 2 * (width - 1);
        eq.generate( &fimpulse [blip_res], half_size );

        auto points = QList<QPointF>();
        for (int i = 0; i < blip_res + half_size; i++) {
            float x = i - (blip_res + half_size);
            x /= blip_res;
            points.append({x, fimpulse[i]});
        }

        // seriously if you don't removeSeries(), Qt tries to redraw the whole fucking chart
        // on every single point you add... even if you pass in a whole list of points at once.
        // who needs transactional behavior or lazy redraw, when you have the chad O(n^2)?
        _chart->removeSeries(_series);
        _series->clear();
        _series->append(points);
        _chart->addSeries(_series);
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
