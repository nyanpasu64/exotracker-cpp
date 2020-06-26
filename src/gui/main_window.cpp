#include "main_window.h"
#include "gui/pattern_editor/pattern_editor_panel.h"
#include "lib/lightweight.h"

#include <fmt/core.h>
#include <rtaudio/RtAudio.h>
#include <verdigris/wobjectimpl.h>

#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QShortcut>
#include <QTimer>

#include <chrono>
#include <functional>  // reference_wrapper
#include <iostream>
#include <optional>
#include <stdexcept>  // logic_error

namespace gui::main_window {

using std::unique_ptr;
using std::make_unique;

using gui::pattern_editor::PatternEditorPanel;

W_OBJECT_IMPL(MainWindow)

static MainWindow * instance;

MainWindow::MainWindow(QWidget *parent) :
    // I kinda regret using the same name for namespace "history" and member variable "history".
    // But it's only a problem since C++ lacks pervasive `self`.
    QMainWindow(parent)
{}

// frac_to_tick() copied from sequencer.h.
// TODO refactor code so we send a beat fraction
// (encoded as int16/int16) to audio thread.

using doc::BeatFraction;
using TickT = int32_t;

struct BeatPlusTick {
    int32_t beat;
    int32_t dtick;
};

static BeatPlusTick frac_to_tick(TickT ticks_per_beat, BeatFraction beat) {
    doc::FractionInt ibeat = beat.numerator() / beat.denominator();
    BeatFraction fbeat = beat - ibeat;

    doc::FractionInt dtick = doc::round_to_int(fbeat * ticks_per_beat);
    return BeatPlusTick{.beat=ibeat, .dtick=dtick};
}

// module-private
class MainWindowImpl : public MainWindow {
    // W_OBJECT(MainWindowImpl)
public:
    // fields
    gui::history::History _history;

    // GUI widgets/etc.
    QScreen * _screen;
    QTimer _gui_refresh_timer;

    // Use raw pointers since QObjects automatically destroy children.
    PatternEditorPanel * _pattern_editor_panel;

    // Global playback shortcuts.
    // TODO implement global configuration system with "reloaded" signal
    QShortcut _play_pause{QKeySequence{Qt::Key_Space}, this};
    QShortcut _restart_audio_shortcut{QKeySequence{Qt::Key_F12}, this};

    // Audio.
    RtAudio _rt;
    unsigned int _audio_device;
    std::optional<AudioThreadHandle> _audio_handle;

    using Clock = std::chrono::steady_clock;
    Clock::time_point _prev_time;

    // impl
    void _() override {}

    MainWindowImpl(doc::Document document, QWidget * parent)
        : MainWindow(parent)
        , _history{std::move(document)}
        , _rt{}
        , _audio_handle{}
    {
        // Setup GUI.
        setup_widgets();  // Output: _pattern_editor_panel.
        _pattern_editor_panel->set_history(_history);

        // Hook up refresh timer.
        connect(
            &_gui_refresh_timer, &QTimer::timeout,
            this, [this] () {
                MaybeSequencerTime maybe_seq_time{};

                auto & audio = audio_handle();
                if (audio.has_value()) {
                    maybe_seq_time = audio->play_time();
                }

                emit gui_refresh(maybe_seq_time);
            }
        );
        setup_screen();
        // TODO setup_screen() when primaryScreen changed
        // TODO setup_timer() when refreshRate changed

        // Setup audio.
        setup_audio();

        connect(
            &_restart_audio_shortcut, &QShortcut::activated,
            this, &MainWindow::restart_audio_thread
        );

        connect(
            &_play_pause, &QShortcut::activated,
            this, [&] {
                if (_audio_handle.has_value()) {
                    // TODO store playback state in GUI thread
                    if (false) {
                        _audio_handle->stop_playback();
                    } else {
                        auto ticks_per_beat = _history
                            .gui_get_document()
                            ->sequencer_options
                            .ticks_per_beat;

                        auto x = frac_to_tick(ticks_per_beat, _cursor_y.beat);

                        SequencerTime time{
                            (uint16_t) _cursor_y.seq_entry_index,
                            // If set to 0, GUI renders pattern wrong briefly.
                            (uint16_t) ticks_per_beat,
                            (int16_t) x.beat,
                            (int16_t) x.dtick
                        };
                        _audio_handle->start_playback(time);
                    }
                }
            }
        );

        connect(
            &_play_pause, &QShortcut::activatedAmbiguously,
            this, [&] {
                qDebug() << "ambiguous yay";
            }
        );

    }

    /// Output: _pattern_editor_panel.
    void setup_widgets() {
        auto w = this;
        {add_central_widget_no_layout(PatternEditorPanel(parent));
            _pattern_editor_panel = w;
        }
    }

    void setup_screen() {
        _screen = QGuiApplication::primaryScreen();
        setup_timer();
    }
    // W_SLOT(setup_screen)

    void setup_timer() {
        // floor div
        auto refresh_ms = int(1000 / _screen->refreshRate());
        _gui_refresh_timer.setInterval(refresh_ms);
        // calling twice will restart timer.
        _gui_refresh_timer.start();
    }
    // W_SLOT(setup_timer)

    /// Output: _audio_device.
    void scan_devices() {
        std::cout << "Devices {\n";

        using fmt::print;

        // Determine the number of devices available
        unsigned int n_devices = _rt.getDeviceCount();

        // Scan through devices for various capabilities
        for (unsigned int i = 0; i < n_devices; i++) {
            print("    {}: ", i);

            RtAudio::DeviceInfo info = _rt.getDeviceInfo(i);
            if (info.probed == true) {
                print(
                    "name={}, rate={}, out_nchan={}\n",
                    info.name,
                    info.preferredSampleRate,
                    info.outputChannels
                );
            } else {
                print("probe failed\n");
            }
        }

        std::cout << "}\n";
        fflush(stdout);

        if (n_devices == 0) {
            std::cout << "No devices available\n";
            return;
        }

        print("Default device index: {}\n", _rt.getDefaultOutputDevice());
        fflush(stdout);

        _audio_device = _rt.getDefaultOutputDevice();
    }

    /// Output: _audio_handle.
    void setup_audio() {
        // Initializes _audio_device.
        scan_devices();

        // Begin playing audio. Destroying this variable makes audio stop.
        _audio_handle = AudioThreadHandle::make(_rt, _audio_device, _history);
    }

    std::optional<AudioThreadHandle> const & audio_handle() override {
        return _audio_handle;
    }

    void restart_audio_thread() override {
        // Only one stream can be running at a time.
        // The lifetimes of the old and new audio thread must not overlap.
        // So destroy the old before constructing the new.
        _audio_handle = {};
        _audio_handle = AudioThreadHandle::make(_rt, _audio_device, _history);
    }
};

// W_OBJECT_IMPL(MainWindowImpl)

// public
std::unique_ptr<MainWindow> MainWindow::make(doc::Document document, QWidget * parent) {
    auto out = make_unique<MainWindowImpl>(std::move(document), parent);
    instance = &*out;
    return out;
}

MainWindow & MainWindow::get_instance() {
    if (instance) {
        return *instance;
    } else {
        throw std::logic_error("Tried to get instance when none was present");
    }
}


MainWindow::~MainWindow() {
    instance = nullptr;
}

// namespace
}
