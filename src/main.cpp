#include "gui/main_window.h"
#include "gui/history.h"
#include "audio.h"

#include <fmt/core.h>
#include <rtaudio/RtAudio.h>

#include <QApplication>

#include <iostream>

#include "win32_fonts.h"

using std::unique_ptr;
using fmt::print;
using gui::MainWindow;

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    win32_set_font();

    gui::history::History history{doc::dummy_document()};

    RtAudio rt;

    std::cout << "Devices {\n";

    // Determine the number of devices available
    unsigned int n_devices = rt.getDeviceCount();

    // Scan through devices for various capabilities
    for (unsigned int i = 0; i < n_devices; i++) {
        print("    {}: ", i);

        RtAudio::DeviceInfo info = rt.getDeviceInfo(i);
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
        return 1;
    }

    print("Default device index: {}\n", rt.getDefaultOutputDevice());
    fflush(stdout);

    // Begin playing audio. Destroying this variable makes audio stop.
    auto audio_handle = audio::output::AudioThreadHandle::make(rt, history);

    unique_ptr<MainWindow> w = MainWindow::make(history);
    w->show();

    return a.exec();
}
