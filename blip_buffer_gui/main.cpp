#include "gui/lib/layout_macros.h"

#include <Blip_Buffer/Blip_Buffer.h>

#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QtCharts>
#include <QChartView>

using namespace QtCharts;

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);


    QWidget _c;
    auto c = &_c;

    QVBoxLayout _l;
    auto l = &_l;

    c->setLayout(l);

    int width = blip_high_quality;
    blip_eq_t eq;

    float fimpulse [blip_res / 2 * (blip_widest_impulse_ - 1) + blip_res * 2] = {0};

    int const half_size = blip_res / 2 * (width - 1);
    eq.generate( &fimpulse [blip_res], half_size );

    QLineSeries *series = new QLineSeries();
    for (int i = 0; i < blip_res + half_size; i++) {
        float x = i - (blip_res + half_size);
        x /= blip_res;
        series->append(x, fimpulse[i]);
    }

    QChart *chart = new QChart();
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

    QChartView * chartView;
    {l__w(QChartView(chart));
        chartView = w;
        w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        chartView->setRenderHint(QPainter::Antialiasing);
    }

    c->resize(800, 600);
    c->show();
    return a.exec();
}
