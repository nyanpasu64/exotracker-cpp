#include "gui/main_window.h"
#include "audio/audio.h"
#include "audio/output.h"

#include <QApplication>
#include <portaudiocpp/PortAudioCpp.hxx>

#ifdef _WIN32
#include <Windows.h>
#endif

using std::unique_ptr;


#include "win32_fonts.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

#ifdef _WIN32
    win32_set_font();
#endif

    portaudio::AutoSystem autoSys;
    portaudio::System & sys = portaudio::System::instance();

    // Begin playing audio. Destroying this variable makes audio stop.
    audio::AudioThreadHandle audio_handle{sys};

    unique_ptr<MainWindow> w = MainWindow::make();
    w->show();

    return a.exec();
}
