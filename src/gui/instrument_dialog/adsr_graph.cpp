#include "adsr_graph.h"
#include "gui/lib/layout_macros.h"
#include "gui/lib/painter_ext.h"
#include "audio/tempo_calc.h"
#include "util/expr.h"

#include <QDebug>
#include <QFontMetrics>
#include <QGridLayout>
#include <QPainter>

#include <algorithm>  // std::transform
#include <cmath>
#include <cstdint>
#include <vector>

namespace gui::instrument_dialog::adsr_graph {

using NsampT = uint32_t;
static NsampT const PERIODS [32] =
{
   0x1'00'00, // never fires
          2048, 1536,
    1280, 1024,  768,
     640,  512,  384,
     320,  256,  192,
     160,  128,   96,
      80,   64,   48,
      40,   32,   24,
      20,   16,   12,
      10,    8,    6,
       5,    4,    3,
             2,
             1
};


struct Point {
    /// An absolute timestamp in samples.
    NsampT time;

    /// An envelope amplitude within the range [0..0x7ff].
    uint32_t new_level;
};

static constexpr uint32_t MAX_LEVEL = 0x7ff;

struct AdsrResult {
    std::vector<Point> envelope;
    Point decay_begin;
    Point sustain_point;
};

static uint32_t sustain_level(Adsr adsr) {
    return (uint32_t(adsr.sustain_level) + 1) << 8;
}

static void iterate_adsr(Adsr adsr, auto & cb) {
    /*
    Based off:

    - https://github.com/nyanpasu64/AddmusicK/blob/master/docs/readme_files/hex_command_reference.html
    - https://problemkaputt.de/fullsnes.htm#snesapudspadsrgainenvelope
        - Note that Level>=7E0h is inaccurate according to SPC_DSP.cpp, it's >0x7FF.
    - 3rdparty/snes9x-dsp/SPC_DSP.cpp in this repo

    ## Non-determinism

    S-DSP envelopes increase and decrease when ticked by a timer.
    The real S-DSP envelope hardware is non-deterministic between notes.
    The envelopes are ticked by a global counter, not a per-channel timer
    reset on note-on. This results in nondeterministic timer tick times
    (switching between timer periods can trigger a tick sooner than 1 period).

    This function's emulation is simplified and inaccurate, but fully deterministic.
    It does not model timer phases, instead pretending the timer phase is reset
    whenever switching timer periods.

    ## How are the various timer periods generated?

    Blargg SPC_DSP's timer, `state_t::counter`, decreases modulo (2048 * 5 * 3).
    At a given `period`, an ADSR tick fires whenever the `counter % period == constant`
    (where the constant varies by whether the period is a multiple of 3, 5, or not).
    I suspect the actual S-DSP chip has separate power-of-2-period timers
    ticked at freq, freq/3, and freq/5, and checks `counters[...] % power-of-2 == 0`.
    */
    NsampT now = 0;
    uint32_t level = 0;

    // Adapted from SPC_DSP.cpp, SPC_DSP::run_envelope().
    enum class EnvMode { Attack, Decay, Decay2 };

    EnvMode env_mode = EnvMode::Attack;

    while (true) {
        size_t period_idx;
        // This function currently only handles ADSR.
        // Using GAIN for exponential release will be simulated in another function.
        // Manually switching between GAIN modes at composer-controlled times
        // is not a planned feature,
        // and if implemented, plotting it will require EventQueue.
        if (env_mode == EnvMode::Attack) {
            period_idx = adsr.attack_rate * 2 + 1;
            level += period_idx < 31 ? 0x20 : 0x400;
        } else
        if (env_mode == EnvMode::Decay) {
            level--;
            level -= level >> 8;
            period_idx = adsr.decay_rate * 2 + 0x10;
        } else
        {
            assert(env_mode == EnvMode::Decay2);
            level--;
            level -= level >> 8;
            period_idx = adsr.decay_2;
        }

        if (period_idx == 0) {
            cb.end();
            return;
        } else {
            now += PERIODS[period_idx];

            // Check for overflow.
            if (env_mode == EnvMode::Attack && level > 0x7FF) {
                level = 0x7FF;
                env_mode = EnvMode::Decay;
                cb.decay_begin(Point{now, level});
            }
            if (level < 0) {
                level = 0;
            }

            // According to Blargg's and Higan's S-DSP emulators,
            // the real hardware checks for Decay2 *before* checking for Decay.
            // If the sustain level is set to 0x7 (100%), this introduces a 1-sample delay
            // between Decay and Decay2, during which the Decay period rather than Decay2
            // is used to determine whether to clock the envelope.
            // This introduces extra nondeterminism based on the timer phase.
            //
            // We check for Decay2 *after* checking for Decay,
            // so we instantly advance from Attack to Decay2.
            // This models the real SNES better, but is more deterministic than it
            // (though it doesn't capture the 1-sample glitch that the real SNES
            // sometimes experiences).
            // Removing this hack would result in using the Decay period for an
            // entire time-step (not just 1 sample), which is bad.
            if (env_mode == EnvMode::Decay && (level >> 8) == adsr.sustain_level) {
                env_mode = EnvMode::Decay2;
                cb.sustain_point(Point{now, sustain_level(adsr)});
            }
        }

        if (!cb.point(Point{now, level})) {
            return;
        }

        if (level == 0) {
            cb.end();
            return;
        }
    }
}

/// Simulates the ADSR of a note.
///
/// Returns a vector of (timestamp, new amplitude at that time), plus metadata:
/// (The real S-DSP envelope is stair-stepped.)
///
/// - The first element is (0, amplitude).
/// - Only the last element's time is >= max_time.
///   (If the amplitude reaches 0 before max_time,
///   the last element's time may be < max_time.)
static AdsrResult adsr(Adsr adsr, NsampT end_time) {
    struct {
        NsampT _end_time;
        std::vector<Point> _envelope = {Point{0, 0}};
        Point _decay_begin{};
        Point _sustain_point{};

    private:
        bool envelope_done() const {
            return _envelope.back().time >= _end_time;
        }

        bool all_done() const {
            // Wait for _decay_begin and _sustain_point to be reached.
            // Otherwise when shrinking the window, the sustain line remains stuck
            // to the right side of the canvas, its left half visible.
            return envelope_done()
                && _decay_begin.time > 0
                && _sustain_point.time > 0;
        }

    public:
        bool point(Point p) {
            if (!envelope_done()) {
                _envelope.push_back(p);
            }
            return !all_done();
        }

        void decay_begin(Point p) {
            _decay_begin = p;
        }

        void sustain_point(Point p) {
            _sustain_point = p;
        }

        void end() {
            auto const& prev = _envelope.back();
            if (prev.time < _end_time) {
                _envelope.push_back(Point{_end_time, prev.new_level});
            }
        }
    } cb {
        ._end_time = end_time,
    };

    iterate_adsr(adsr, cb);

    return AdsrResult {
        std::move(cb._envelope),
        cb._decay_begin,
        cb._sustain_point,
    };
}

// TODO when implementing release GAIN, add
// fn release_gain(adsr: &[]Point, release_time: NsampT, gain: u8) -> []Point
// where the starting point is { .time = release_time, .ampl = adsr[...].ampl }.

QToolButton * small_button(const QString &text, QWidget *parent = nullptr) {
    auto w = new QToolButton(parent);
    w->setText(text);
    return w;
}

constexpr qreal DEFAULT_PX_PER_S = 64.;

AdsrGraph::AdsrGraph(QWidget * parent)
    : QWidget(parent)
    , _px_per_s(DEFAULT_PX_PER_S)
    , _adsr(doc::DEFAULT_ADSR)
{
    // setMouseTracking() (not called) generates paint events on mouse move.

    auto grid = new QGridLayout(this);
    grid->setSpacing(0);

    int row = 0;
    int col = 0;
    grid->setColumnStretch(col++, 1);

    _zoom_out = small_button("-");
    grid->addWidget(_zoom_out, row, col++);

    _reset_zoom = small_button("0");
    grid->addWidget(_reset_zoom, row, col++);

    _zoom_in = small_button("+");
    grid->addWidget(_zoom_in, row, col++);

    row++;
    col = 0;
    grid->setRowStretch(row, 1);
    (void) col;

    connect(_zoom_out, &QAbstractButton::pressed, this, &AdsrGraph::zoom_out);
    connect(_zoom_in, &QAbstractButton::pressed, this, &AdsrGraph::zoom_in);
    connect(_reset_zoom, &QAbstractButton::pressed, this, &AdsrGraph::reset_zoom);
}

// TODO separate ADSR-only StateTransaction, calling update()? idk...
void AdsrGraph::set_adsr(Adsr adsr) {
    _adsr = adsr;
    update();
}

void AdsrGraph::zoom_out() {
    _px_per_s /= 2.0;
    update();
}

void AdsrGraph::zoom_in() {
    _px_per_s *= 2.0;
    update();
}

void AdsrGraph::reset_zoom() {
    _px_per_s = DEFAULT_PX_PER_S;
    update();
}

QSize AdsrGraph::sizeHint() const {
    return QSize(360, 240);
}

QSize AdsrGraph::minimumSizeHint() const{
    return sizeHint();
}

static constexpr qreal SMP_PER_S = (qreal) audio::tempo_calc::SAMPLES_PER_S_IDEAL;

static constexpr int TOP_PAD = 4;
static constexpr int BOTTOM_PAD = 0;
static constexpr int LEFT_PAD = 1;
static constexpr int RIGHT_PAD = 0;

static constexpr qreal LINE_WIDTH = 1.5;
static constexpr qreal BG_LINE_WIDTH = LINE_WIDTH;

static constexpr qreal X_TICK_WIDTH = 1.0;
static constexpr int X_TICK_HEIGHT = 6;
static constexpr int X_TICK_STEP_MIN = 32;
static constexpr int NUMBER_MARGIN = 12;

/// Darken a color while preserving its HSL saturation.
/// Properly darkening a pastel color blended with white
/// turns it into a fully saturated color.
static QColor darken(QColor bg, qreal factor = 4.) {
    qreal h, s, l, a;
    bg.getHslF(&h, &s, &l, &a);
    // in HSL, the saturation of a color including at least one 255 is 1.

    return QColor::fromHslF(h, s, l / factor, a);
}

void AdsrGraph::paintEvent(QPaintEvent *event) {
    if (!isEnabled()) {
        return;
    }

    using gui::lib::painter_ext::DrawText;

    auto painter = QPainter(this);

    int full_w = width();
    int full_h = height();

    painter.setRenderHint(QPainter::Antialiasing);

    painter.translate(LEFT_PAD, TOP_PAD);
    int w = full_w - LEFT_PAD + RIGHT_PAD;
    int h = full_h - TOP_PAD + BOTTOM_PAD;

    // The line covers the entire width.
    // (w: px) / (_zoom: px/s) * (32000 smp/s) : smp
    auto const max_time =
        NsampT(ceil(qreal(w) / qreal(_px_per_s) * SMP_PER_S));

    // Compute the envelope.
    auto env = adsr(_adsr, max_time);

    auto scale_x = [&](NsampT time) -> qreal {
        // Position the line relative to x=0.
        // (p.time: smp) / (32000 smp/s) * (px/s) : px
        return time / SMP_PER_S * _px_per_s;
    };

    auto scale_y = [&](uint32_t level) -> qreal {
        qreal y_rel = 1. - (level / qreal(MAX_LEVEL));
        // The line covers the entire height.
        return y_rel * h;
    };

    auto point_to_qpointf = [&](Point p) -> QPointF {
        return QPointF(scale_x(p.time), scale_y(p.new_level));
    };

    // Draw background colors and vertical lines.
    {
        auto bg_rect = QRectF(-LEFT_PAD, -TOP_PAD, full_w, full_h);
        painter.fillRect(bg_rect, BG_ATTACK);

        auto draw_region = [](QPainter & painter, QRectF & rect, QColor color) {
            if (rect.isValid()) {
                painter.fillRect(rect, color);
            }
            painter.setPen(QPen(darken(color), BG_LINE_WIDTH));
            painter.drawLine(rect.topLeft(), rect.bottomLeft());
        };

        if (env.decay_begin.time != 0) {
            bg_rect.setLeft(scale_x(env.decay_begin.time));
            draw_region(painter, bg_rect, BG_DECAY);
        }

        if (env.sustain_point.time != 0) {
            bg_rect.setLeft(scale_x(env.sustain_point.time));
            draw_region(painter, bg_rect, BG_DECAY2);
        }
    }

    // Draw sustain level line.
    {
        painter.setPen(QPen(darken(BG_SUSTAIN, 3.), LINE_WIDTH));

        QPointF left = point_to_qpointf(env.sustain_point);
        QPointF right = left;
        right.setX(w + RIGHT_PAD);
        painter.drawLine(left, right);
    }

    // Draw envelope line.
    // Strictly speaking this is wrong, the line should hold the old level
    // until reaching the new point's time, then jump to the new level (ZOH).
    // In practice it looks close enough.
    {
        std::vector<QPointF> path;
        std::transform(
            env.envelope.begin(), env.envelope.end(),
            back_inserter(path),
            point_to_qpointf);
        painter.setPen(QPen(Qt::black, LINE_WIDTH));
        painter.drawPolyline(path.data(), (int) path.size());
    }

    // Draw ticks and labels.
    {
        painter.setPen(QPen(Qt::black, X_TICK_WIDTH));
        QFont label_font = font();
        label_font.setPointSizeF(9.0);
        painter.setFont(label_font);

        auto draw_text = DrawText(painter.font());
        int const x_tick_step = EXPR(
            auto fm = QFontMetrics(painter.font());
            int width = fm.boundingRect(QLatin1String("0.125")).width();
            int x_tick_step = X_TICK_STEP_MIN;
            while (x_tick_step < width + width / 4) {
                x_tick_step *= 2;
            }
            return x_tick_step;
        );

        for (int x = 0; x <= w + x_tick_step; x += x_tick_step) {
            Qt::Alignment align;
            if (x == 0)
                align = Qt::AlignLeft;
            else
                align = Qt::AlignHCenter;

            painter.drawLine(x, h, x, h - X_TICK_HEIGHT);
            auto text = QString::number(x / qreal(_px_per_s), 'g', 3);
            draw_text.draw_text(painter, x, h - NUMBER_MARGIN, (int) align, text);
        }
    }
}

// TODO implement zoom buttons or mouse scroll

} // namespace
