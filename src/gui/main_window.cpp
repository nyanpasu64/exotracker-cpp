#include "main_window.h"
#include "gui/pattern_editor/pattern_editor_panel.h"
#include "lib/lightweight.h"
#include "audio.h"

#include <fmt/core.h>
#include <rtaudio/RtAudio.h>
#include <verdigris/wobjectimpl.h>

#include <iostream>
#include <optional>
#include <stdexcept>  // logic_error

namespace gui {

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

using audio::output::AudioThreadHandle;

// module-private
class MainWindowImpl : public MainWindow {
public: // module-private
    // widget pointers go here, if needed

    gui::history::History _history;

    PatternEditorPanel * _pattern_editor_panel;

    RtAudio _rt;
    unsigned int _audio_device;
    std::optional<audio::output::AudioThreadHandle> _audio_handle;

    MainWindowImpl(doc::Document document, QWidget * parent)
        : MainWindow(parent)
        , _history{std::move(document)}
        , _rt{}
        , _audio_handle{}
    {
        setupUi();
        _pattern_editor_panel->set_history(_history);

        setup_audio();
    }

    void _() override {}

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

    /// Output: _pattern_editor_panel.
    void setupUi() {
        auto w = this;
        {add_central_widget_no_layout(PatternEditorPanel(parent));
            _pattern_editor_panel = w;
        }
    }

    void restart_audio_thread() override {
        // Only one stream can be running at a time.
        // The lifetimes of the old and new audio thread must not overlap.
        // So destroy the old before constructing the new.
        _audio_handle = {};
        _audio_handle = AudioThreadHandle::make(_rt, _audio_device, _history);
    }
};

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
