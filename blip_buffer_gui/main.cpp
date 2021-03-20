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
using std::move;

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);


    QWidget _c;
    auto c = &_c;

    QVBoxLayout _l;
    auto l = &_l;

    c->setLayout(l);

    QChart *chart = new QChart();
    QChartView * chartView;
    {l__w(QChartView(MOVE chart));
        chartView = w;
        w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        chartView->setRenderHint(QPainter::Antialiasing);
    }

    QLineSeries *series = new QLineSeries();
    chart->legend()->hide();
    chart->addSeries(series);
    chart->setTitle("blip_buffer");

    QValueAxis *axisX = new QValueAxis;
    axisX->setTickAnchor(0);
    axisX->setTickInterval(0.5);
    axisX->setTickType(QValueAxis::TicksDynamic);
    axisX->setLabelFormat("%.1f");
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    QValueAxis *axisY = new QValueAxis;
    axisY->setTickAnchor(0);
    axisY->setTickInterval(2048);
    axisY->setTickType(QValueAxis::TicksDynamic);
    axisY->setLabelFormat("%.0f");
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    QSlider * width_nsamp;
    QSlider * treble_db;
    QSlider * rolloff_freq;
    QSlider * sample_rate;
    QSlider * cutoff_freq;
    {l__c_form(QWidget, QFormLayout);
        {form__label_w("Half-width (samples)", QSlider);
            width_nsamp = w;
            w->setOrientation(Qt::Horizontal);
            w->setRange(8, 32);
            w->setValue(16);
        }
        {form__label_w("Treble (Nyquist?) attenuation (dB)", QSlider);
            treble_db = w;
            w->setOrientation(Qt::Horizontal);
            w->setRange(-90, 5);
            w->setValue(-24);  // famitracker's default value... an empty blip_eq_t() defaults to 0.
        }
        {form__label_w("Treble attenuation frequency", QSlider);
            rolloff_freq = w;
            w->setOrientation(Qt::Horizontal);
            w->setRange(0, 48000);
            w->setValue(12000);  // famitracker's default value... an empty blip_eq_t() defaults to 0.
        }
        {form__label_w("Sample rate", QSlider);
            sample_rate = w;
            w->setOrientation(Qt::Horizontal);
            w->setRange(0, 96000);
            w->setValue(48000);
        }
        {form__label_w("Cutoff frequency (?)", QSlider);
            cutoff_freq = w;
            w->setOrientation(Qt::Horizontal);
            w->setRange(0, 48000);
            w->setValue(0);  // idk what this does, famitracker doesn't supply it, defaults to 0.
        }
    }

    bool dirty = true;
    auto force_draw = [&]() {
        throw "up";
        dirty = false;
        int width = width_nsamp->value();
        auto eq = blip_eq_t(
            treble_db->value(), rolloff_freq->value(), sample_rate->value(), cutoff_freq->value()
        );

        float fimpulse [blip_res / 2 * (blip_widest_impulse_ - 1) + blip_res * 2] = {0};

        int const half_size = blip_res / 2 * (width - 1);
        eq.generate( &fimpulse [blip_res], half_size );

        series->clear();
        for (int i = 0; i < blip_res + half_size; i++) {
            float x = i - (blip_res + half_size);
            x /= blip_res;
            series->append(x, fimpulse[i]);
        }
    };

    auto draw = [&] () {
        if (!dirty) {
            dirty = true;
            QMetaObject::invokeMethod(series, force_draw, Qt::QueuedConnection);
        }
    };
    QObject::connect(width_nsamp, &QSlider::valueChanged, series, draw);
    QObject::connect(treble_db, &QSlider::valueChanged, series, draw);
    QObject::connect(rolloff_freq, &QSlider::valueChanged, series, draw);
    QObject::connect(sample_rate, &QSlider::valueChanged, series, draw);
    QObject::connect(cutoff_freq, &QSlider::valueChanged, series, draw);

    c->resize(800, 600);
    c->show();
    return a.exec();
}
