#include "adsr_graph.h"
#include "gui/lib/layout_macros.h"
#include "gui/lib/painter_ext.h"
#include "gui_common.h"
#include "audio/tempo_calc.h"
#include "util/release_assert.h"
#include "util/expr.h"

#include <gsl/span>

#include <QApplication>
#include <QDebug>
#include <QFontMetrics>
#include <QGridLayout>
#include <QWheelEvent>
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
    uint32_t level;
};

static constexpr uint32_t MAX_LEVEL = 0x7ff;

struct AdsrResult {
    std::vector<Point> envelope;
    size_t decay_idx;
    size_t sustain_idx;
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
        }

        now += PERIODS[period_idx];

        auto decay_begin = Point{};
        auto sustain_point = Point{};

        // Check for overflow.
        if (env_mode == EnvMode::Attack && level > 0x7FF) {
            level = 0x7FF;
            env_mode = EnvMode::Decay;
            decay_begin = Point{now, level};
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
            sustain_point = Point{now, sustain_level(adsr)};
        }

        if (!cb.point(Point{now, level})) {
            return;
        }
        if (decay_begin.time != 0) {
            if (!cb.decay_begin(decay_begin)) {
                return;
            }
        }
        if (sustain_point.time != 0) {
            if (!cb.sustain_point(sustain_point)) {
                return;
            }
        }

        if (level == 0) {
            cb.end();
            return;
        }
    }
}

/// Simulates the ADSR of a note.
///
/// Returns a vector of (timestamp, amplitude), plus metadata:
///
/// - The first element is (0, amplitude).
/// - Each change in level produces two points, (time, old amplitude)
///   and (time, new amplitude), so stairsteps are plotted properly.
/// - The last element's time is >= end_time. (Earlier elements might be >= end_time.)
static AdsrResult get_adsr(Adsr adsr, NsampT end_time) {
    struct {
        NsampT _end_time;
        std::vector<Point> _envelope = {Point{0, 0}};

        size_t _decay_idx{};
        size_t _sustain_idx{};

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
                // TODO make stairsteps toggleable
                _envelope.push_back(Point {
                    .time = p.time,
                    .level = _envelope.back().level,
                });
                _envelope.push_back(p);
            }
            return !all_done();
        }

        /// Called after the corresponding point() with the same timestamp.
        bool decay_begin(Point p) {
            _decay_begin = p;
            _decay_idx = _envelope.size() - 1;  // _envelope starts at size 1.
            return !all_done();
        }

        /// Called after the corresponding point() with the same timestamp.
        bool sustain_point(Point p) {
            _sustain_point = p;
            _sustain_idx = _envelope.size() - 1;  // _envelope starts at size 1.
            return !all_done();
        }

        void end() {
            auto const& prev = _envelope.back();
            if (prev.time < _end_time) {
                _envelope.push_back(Point{_end_time, prev.level});
            }
        }
    } cb {
        ._end_time = end_time,
    };

    iterate_adsr(adsr, cb);

    return AdsrResult {
        std::move(cb._envelope),
        cb._decay_idx,
        cb._sustain_idx,
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

AdsrGraph::AdsrGraph(QWidget * parent)
    : QWidget(parent)
    , _zoom_level(0)
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

    _zoom_reset = small_button("0");
    grid->addWidget(_zoom_reset, row, col++);

    _zoom_in = small_button("+");
    grid->addWidget(_zoom_in, row, col++);

    row++;
    col = 0;
    grid->setRowStretch(row, 1);
    (void) col;

    connect(_zoom_out, &QAbstractButton::pressed, this, &AdsrGraph::zoom_out);
    connect(_zoom_in, &QAbstractButton::pressed, this, &AdsrGraph::zoom_in);
    connect(_zoom_reset, &QAbstractButton::pressed, this, &AdsrGraph::zoom_reset);

    setFocusPolicy(Qt::StrongFocus);

    auto & options = get_app().options();
    _zoom_out_key = new QShortcut(options.global_keys.zoom_out, this);
    _zoom_in_key = new QShortcut(options.global_keys.zoom_in, this);
    _zoom_reset_key = new QShortcut(QKeySequence("Ctrl+0"), this);

    connect(_zoom_out_key, &QShortcut::activated, this, &AdsrGraph::zoom_out);
    connect(_zoom_in_key, &QShortcut::activated, this, &AdsrGraph::zoom_in);
    connect(_zoom_reset_key, &QShortcut::activated, this, &AdsrGraph::zoom_reset);
}

// TODO separate ADSR-only StateTransaction, calling update()? idk...
void AdsrGraph::set_adsr(Adsr adsr) {
    _adsr = adsr;
    update();
}

constexpr int NUM_PER_WHEEL_CLICK = 120;

static void clamp_zoom(int & zoom_level) {
    // At zoom -4, the minimum window size shows 90 seconds
    // which is enough to show the longest possible ADSR curve.
    // At zoom 11, the window is zoomed in enough to see individual samples.
    zoom_level =
        std::clamp(zoom_level, -4 * NUM_PER_WHEEL_CLICK, 11 * NUM_PER_WHEEL_CLICK);
}

void AdsrGraph::zoom_out() {
    _zoom_level -= NUM_PER_WHEEL_CLICK;
    clamp_zoom(_zoom_level);
    update();
}

void AdsrGraph::zoom_in() {
    _zoom_level += NUM_PER_WHEEL_CLICK;
    clamp_zoom(_zoom_level);
    update();
}

void AdsrGraph::zoom_reset() {
    _zoom_level = 0;
    update();
}

void AdsrGraph::wheelEvent(QWheelEvent * event) {
    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        int dy = event->angleDelta().y();
        _zoom_level += dy;
        clamp_zoom(_zoom_level);
        update();
        event->accept();
    } else {
        QWidget::wheelEvent(event);
    }
}

constexpr qreal DEFAULT_PX_PER_S = 64.;

qreal AdsrGraph::get_px_per_s() {
    return DEFAULT_PX_PER_S * pow(2.0, qreal(_zoom_level) / qreal(NUM_PER_WHEEL_CLICK));
}

QSize AdsrGraph::sizeHint() const {
    return QSize(360, 240);
}

QSize AdsrGraph::minimumSizeHint() const{
    return sizeHint();
}

static constexpr qreal SMP_PER_S = (qreal) audio::tempo_calc::SAMPLES_PER_S_IDEAL;

static constexpr qreal LINE_WIDTH = 1.5;
static constexpr qreal BG_LINE_WIDTH = LINE_WIDTH;

static constexpr qreal TOP_PAD = 12;
static constexpr qreal BOTTOM_PAD = 0;
static constexpr qreal LEFT_PAD = 2;
static constexpr qreal RIGHT_PAD = 0;

static constexpr qreal X_TICK_WIDTH = 1.0;
static constexpr qreal MAJOR_TICK_HEIGHT = 6;
static constexpr qreal MINOR_TICK_HEIGHT = 3;

static constexpr qreal PX_PER_X_TICK = 64;
static constexpr qreal NUMBER_DY = 12;

using gui::lib::docs_palette::Hue;
using gui::lib::docs_palette::Shade;
using gui::lib::docs_palette::get_color;
using gui::lib::docs_palette::get_gray;

static QColor bg_color(Hue hue) {
    return get_color(hue, qreal(Shade::White) - 0.5);
}

static QColor bg_line_color(Hue hue) {
    return get_color(hue, 5);
}

static QColor fg_color(Hue hue) {
    return get_color(hue, 2);
}

static QPointF with_x(QPointF p, qreal x) {
    p.setX(x);
    return p;
}

static QPointF with_y(QPointF p, qreal y) {
    p.setY(y);
    return p;
}

template<typename T>
gsl::span<T> safe_slice(gsl::span<T> span, size_t begin, size_t end) {
    begin = std::min(begin, span.size());
    end = std::clamp(end, begin, span.size());
    return span.subspan(begin, end - begin);
}

struct TickSpacing {
    int exponent;
    int minor;
    int major;
};

TickSpacing get_tick_spacing(qreal range) {
    // Based on https://stackoverflow.com/q/8506881.

    auto exponent = (int) floor(log10(range));
    qreal fraction = range / pow(10.f, exponent);

    int minor, major;
    if (fraction < 1.5) {
        exponent--;
        minor = 5;
        major = 10;
    } else if (fraction < 3) {
        minor = 1;
        major = 2;
    } else if (fraction < 7) {
        minor = 1;
        major = 5;
    } else {
        minor = 5;
        major = 10;
    }

    return TickSpacing {
        .exponent = exponent,
        .minor = minor,
        .major = major,
    };
}

void AdsrGraph::paintEvent(QPaintEvent *) {
    // Based off https://github.com/nyanpasu64/AddmusicK/blob/master/docs/readme_files/hex_command_reference.html
    if (!isEnabled()) {
        return;
    }

    auto image_size = size() * devicePixelRatioF();
    image_size.setHeight(1);
    if (_bg_colors.size() != image_size) {
        _bg_colors = QImage(image_size, QImage::Format_RGB32);
    }
    _bg_colors.setDevicePixelRatio(devicePixelRatioF());

    using gui::lib::painter_ext::DrawText;

    auto painter = QPainter(this);

    int full_w = width();
    int full_h = height();

    painter.setRenderHint(QPainter::Antialiasing);

    painter.translate(LEFT_PAD, TOP_PAD);
    qreal const w = full_w - LEFT_PAD - RIGHT_PAD;
    qreal const h = full_h - TOP_PAD - BOTTOM_PAD;

    qreal const px_per_s = get_px_per_s();

    // The line covers the entire width.
    // (w: px) / (_zoom: px/s) * (32000 smp/s) : smp
    auto const max_time =
        NsampT(ceil(qreal(w) / qreal(px_per_s) * SMP_PER_S));

    // Compute the envelope.
    auto adsr = get_adsr(_adsr, max_time);

    auto scale_x = [&](qreal time) -> qreal {
        // Position the line relative to x=0.
        // (p.time: smp) / (32000 smp/s) * (px/s) : px
        return time / SMP_PER_S * px_per_s;
    };

    auto scale_y = [&](uint32_t level) -> qreal {
        qreal y_rel = 1. - (level / qreal(MAX_LEVEL));
        // The line covers the entire height.
        return y_rel * h;
    };

    auto point_to_qpointf = [&](Point p) -> QPointF {
        return QPointF(scale_x(p.time), scale_y(p.level));
    };

    // Draw background lines.
    using gui::lib::docs_palette::get_color;
    using gui::lib::docs_palette::Shade;
    using namespace colors;
    {
        auto bg_rect = QRectF(-LEFT_PAD, -TOP_PAD, full_w, full_h);
        painter.fillRect(bg_rect, Qt::white);
    }

    // Compute envelope line.
    std::vector<QPointF> path_vec;
    std::transform(
        adsr.envelope.begin(), adsr.envelope.end(),
        std::back_inserter(path_vec),
        point_to_qpointf);

    // Fill envelope background, adding an extra point at the bottom right.
    {
        // Fill image with color of background.
        _bg_colors.fill(bg_color(colors::ATTACK));

        auto painter = QPainter(&_bg_colors);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(LEFT_PAD, 0);

        // Add extra width, to prevent rounding errors when setting left
        // from causing gaps on the right.
        auto bg_rect = QRectF(0, 0, w + 100, 1);

        bg_rect.setLeft(scale_x(adsr.decay_begin.time));
        painter.fillRect(bg_rect, bg_color(colors::DECAY));

        bg_rect.setLeft(scale_x(adsr.sustain_point.time));
        painter.fillRect(bg_rect, bg_color(colors::DECAY2));
    }
    path_vec.push_back(QPointF(w, h));
    painter.setPen(Qt::NoPen);
    painter.setBrush(EXPR(
        auto bg_colors = QBrush(_bg_colors);
        bg_colors.setTransform(painter.transform().inverted());
        return bg_colors;
    ));
    painter.drawPolygon(path_vec.data(), (int) path_vec.size());

    QPointF const decay_begin = point_to_qpointf(adsr.decay_begin);
    QPointF const sustain_point = point_to_qpointf(adsr.sustain_point);

    // Draw attack vertical line.
    painter.setPen(QPen(get_gray(4), BG_LINE_WIDTH));
    painter.drawLine(decay_begin, with_y(decay_begin, h));

    // Draw SL vertical/horizontal line.
    painter.setPen(QPen(bg_line_color(SUSTAIN), BG_LINE_WIDTH));
    painter.drawLine(sustain_point, with_y(sustain_point, h));
    painter.drawLine(sustain_point, with_x(sustain_point, w + RIGHT_PAD));

    // Draw envelope line, excluding extra point.
    {
        auto plot_line = [&painter](gsl::span<QPointF const> path, Hue hue) {
            if (path.size() <= 1) {
                return;
            }
            painter.setPen(QPen(fg_color(hue), LINE_WIDTH));
            painter.drawPolyline(path.data(), (int) path.size());
        };

        auto path = gsl::span(path_vec.data(), adsr.envelope.size());
        plot_line(safe_slice(path, 0, adsr.decay_idx + 1), colors::ATTACK);
        plot_line(safe_slice(path, adsr.decay_idx, adsr.sustain_idx + 1), colors::DECAY);
        plot_line(safe_slice(path, adsr.sustain_idx, path.size()), colors::DECAY2);
    }

    // Draw ticks and labels.
    {
        painter.setPen(QPen(Qt::black, X_TICK_WIDTH));
        painter.setFont(EXPR(
            QFont label_font = font();
            label_font.setPointSizeF(9.0);
            return label_font;
        ));
        auto draw_text = DrawText(painter.font());

        TickSpacing s_per_tick_rounded = EXPR(
            auto s_per_tick = PX_PER_X_TICK / px_per_s;
            return get_tick_spacing(s_per_tick);
        );

        auto const max_x = scale_x(max_time);
        int digit_s = 0;
        int major_counter = 0;
        int const modulo = s_per_tick_rounded.major / s_per_tick_rounded.minor;

        while (true) {
            Qt::Alignment align;
            if (digit_s == 0)
                align = Qt::AlignLeft;
            else
                align = Qt::AlignHCenter;

            auto tick_s = qreal(digit_s) * pow(10, s_per_tick_rounded.exponent);
            auto x = scale_x(tick_s * SMP_PER_S);

            auto draw_tick = [&painter, h](qreal x, qreal tick_height) {
                x = qRound(x + 0.499) - 0.5;
                painter.drawLine(QPointF(x, h), QPointF(x, h - tick_height));
            };

            if (major_counter == 0) {
                draw_tick(x, MAJOR_TICK_HEIGHT);
                auto text = QString::number(x / px_per_s);
                draw_text.draw_text(painter, x - 0.5, h - NUMBER_DY, (int) align, text);
            } else {
                draw_tick(x, MINOR_TICK_HEIGHT);
            }

            if (x >= max_x) {
                break;
            }

            digit_s += s_per_tick_rounded.minor;
            major_counter++;
            if (major_counter >= modulo) {
                major_counter = 0;
            }
        }
    }
}

// TODO implement zoom buttons or mouse scroll

} // namespace
