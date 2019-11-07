#include "gui/main_window.h"
#include "audio.h"
#include "gui/document_history.h"

#include <QApplication>
#include <portaudiocpp/PortAudioCpp.hxx>

#ifdef _WIN32
#include <Windows.h>
#endif

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

    portaudio::AutoSystem autoSys;
    portaudio::System & sys = portaudio::System::instance();

    // Begin playing audio. Destroying this variable makes audio stop.
    auto audio_handle = audio::output::AudioThreadHandle::make(sys);

    unique_ptr<MainWindow> w = MainWindow::make();
    w->show();

    return a.exec();
}
