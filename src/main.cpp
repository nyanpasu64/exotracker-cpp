#include "gui/main_window.h"
#include "gui/history.h"
#include "audio.h"

#include <QApplication>
#include <portaudiocpp/PortAudioCpp.hxx>

#ifdef _WIN32
#include <Windows.h>
#endif

#include <iostream>

using std::unique_ptr;
using gui::MainWindow;

#include "win32_fonts.h"

using gui::MainWindow;

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

#ifdef _WIN32
    win32_set_font();
#endif
    gui::history::History history{doc::dummy_document()};

    portaudio::AutoSystem autoSys;
    portaudio::System & sys = portaudio::System::instance();

    std::cout << "APIs {\n";
    for (auto x = sys.hostApisBegin(), y = sys.hostApisEnd(); x != y; ++x) {
        std::cout << x->name() << "\n";
    }
    std::cout << "} APIs\n";
    std::cout << "Default API: " << sys.defaultHostApi().name() << "\n";
    std::cout.flush();

    // Begin playing audio. Destroying this variable makes audio stop.
    auto audio_handle = audio::output::AudioThreadHandle::make(sys, history);

    unique_ptr<MainWindow> w = MainWindow::make(history);
    w->show();

    return a.exec();
}
