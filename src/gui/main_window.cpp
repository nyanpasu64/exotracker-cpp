#include "main_window.h"
#include "gui/pattern_editor/pattern_editor_panel.h"
#include "lib/lightweight.h"
#include "audio_cmd.h"
#include "util/release_assert.h"

#include <fmt/core.h>
#include <rtaudio/RtAudio.h>
#include <verdigris/wobjectimpl.h>

#include <QAction>
#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
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

using doc::BeatFraction;
using audio_cmd::CommandQueue;
using audio_cmd::AudioCommand;

// module-private
class MainWindowImpl : public MainWindow {
     W_OBJECT(MainWindowImpl)
public:
    // fields
    gui::history::History _history;

    // GUI widgets/etc.
    QScreen * _screen;
    QTimer _gui_refresh_timer;

    // Use raw pointers since QObjects automatically destroy children.
    PatternEditorPanel * _pattern_editor_panel;

    // Global playback shortcuts.
    // TODO implement global configuration system with "reloaded" signal.
    // When user changes shortcuts, reassign shortcut keybinds.

    // QShortcut is only a shortcut. QAction can be bound to menus and buttons too.
    QAction _play_pause{nullptr};
    QAction _play_from_row{nullptr};
    QAction _restart_audio{nullptr};

    /// This API is a bit too broad for my liking, but whatever.
    class AudioComponent {
        // GUI/audio communication.
        AudioState _audio_state = AudioState::Stopped;
        CommandQueue _command_queue;

    public:
        bool is_empty() const {
            return _command_queue.begin() == _command_queue.end();
        }

        /// Return a command to be sent to the audio thread.
        /// It ignores the command's contents,
        /// but monitors its "next" pointer for new commands.
        AudioCommand * stub_command() {
            return _command_queue.begin();
        }

        AudioState audio_state() const {
            return _audio_state;
        }

        void reset() {
            _command_queue.clear();
            _audio_state = AudioState::Stopped;
        }

        void play_pause(MainWindowImpl & win) {
            if (win._audio_handle.has_value()) {
                gc_command_queue(win._audio_handle.value());

                if (_audio_state == AudioState::Stopped) {
                    auto cursor = win._cursor_y;
                    cursor.beat = 0;
                    play_from(win, cursor);
                } else {
                    stop_play(win);
                }
            }
        }

        void play_from_row(MainWindowImpl & win) {
            if (win._audio_handle.has_value()) {
                gc_command_queue(win._audio_handle.value());

                if (_audio_state == AudioState::Stopped) {
                    play_from(win, win._cursor_y);
                } else {
                    stop_play(win);
                }
            }
        }

        void play_from(MainWindowImpl & win, PatternAndBeat time) {
            _command_queue.push(audio_cmd::SeekTo{time});
            _audio_state = AudioState::Starting;

            // Move cursor to right spot, while waiting for audio thread to respond.
            win._cursor_y = time;
        }

        void stop_play([[maybe_unused]] MainWindowImpl & win) {
            _command_queue.push(std::nullopt);
            _audio_state = AudioState::Stopped;
        }

        void gc_command_queue(AudioThreadHandle & audio_handle) {
            // Every time GUI pushes an event, it moves _command_queue.end().
            // Once the audio thread is done processing events,
            // the GUI thread's next call to gc_command_queue()
            // will advance _command_queue.begin().
            // To run code once after the audio thread catches up on events,
            // check if we drain 1+ event, then end with an empty queue.

            if (_command_queue.begin() != _command_queue.end()) {
                auto x = audio_handle.seen_command();
                while (_command_queue.begin() != x) {
                    _command_queue.pop();
                }
                if (_command_queue.begin() == _command_queue.end()) {
                    // once GUI sees audio caught up on commands, it must see audio's new time.
                    if (_audio_state == AudioState::Starting) {
                        _audio_state = AudioState::PlayHasStarted;
                    }
                }
            }
        }
    };

    AudioComponent _audio_component;

    // Audio.
    RtAudio _rt;
    unsigned int _curr_audio_device;

    // Points to _history and _audio_component, must be listed after them.
    std::optional<AudioThreadHandle> _audio_handle;

    // Used to compute GUI redraw FPS. Currently unused.
    using Clock = std::chrono::steady_clock;
    Clock::time_point _prev_time;

    // impl MainWindow
    void _() override {}

    std::optional<AudioThreadHandle> const & audio_handle() override {
        return _audio_handle;
    }

    AudioState audio_state() const override {
        return _audio_component.audio_state();
    }

    void restart_audio_thread() override {
        // Only one stream can be running at a time.
        // The lifetimes of the old and new audio thread must not overlap.
        // So destroy the old before constructing the new.
        _audio_handle = {};
        _audio_component.reset();
        _audio_handle = AudioThreadHandle::make(
            _rt, _curr_audio_device, _history, _audio_component.stub_command()
        );
    }

    // private methods
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

                if (_audio_handle.has_value()) {
                    auto & audio_handle = _audio_handle.value();

                    _audio_component.gc_command_queue(audio_handle);

                    if (_audio_component.audio_state() == AudioState::PlayHasStarted) {
                        maybe_seq_time = audio_handle.play_time();
                    }
                }

                emit gui_refresh(maybe_seq_time);
            }
        );
        setup_screen();
        // TODO setup_screen() when primaryScreen changed
        // TODO setup_timer() when refreshRate changed

        // Setup audio.
        setup_audio();

        auto init_qaction = [&] (QAction & action, QKeySequence seq) {
            action.setShortcut(seq);
            action.setShortcutContext(Qt::WidgetWithChildrenShortcut);
            _pattern_editor_panel->addAction(&action);
        };

        init_qaction(_play_pause, QKeySequence{Qt::Key_Return});
        connect(
            &_play_pause, &QAction::triggered,
            this, [this] () { _audio_component.play_pause(*this); }
        );

        init_qaction(_play_from_row, QKeySequence{Qt::Key_Apostrophe});
        connect(
            &_play_from_row, &QAction::triggered,
            this, [this] () { _audio_component.play_from_row(*this); }
        );

        _restart_audio.setShortcut(QKeySequence{Qt::Key_F12});
        _restart_audio.setShortcutContext(Qt::ShortcutContext::ApplicationShortcut);
        this->addAction(&_restart_audio);
        connect(
            &_restart_audio, &QAction::triggered,
            this, &MainWindow::restart_audio_thread
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

        _curr_audio_device = _rt.getDefaultOutputDevice();
    }

    /// Output: _audio_handle.
    void setup_audio() {
        // Initializes _audio_device.
        scan_devices();

        release_assert(_audio_component.is_empty());

        // Begin playing audio. Destroying this variable makes audio stop.
        _audio_handle = AudioThreadHandle::make(
            _rt, _curr_audio_device, _history, _audio_component.stub_command()
        );
    }
};

W_OBJECT_IMPL(MainWindowImpl)

// public
std::unique_ptr<MainWindow> MainWindow::make(doc::Document document, QWidget * parent) {
    if (instance) {
        throw std::logic_error("Tried to create two MainWindow instances");
    }
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
