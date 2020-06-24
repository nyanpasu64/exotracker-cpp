#ifndef MAINWINDOW_H
#define MAINWINDOW_H

// Do *not* include any other widgets in this header and create an include cycle.
// Other widgets include main_window.h, since they rely on MainWindow for data/signals.
#include "history.h"
#include "doc.h"
#include "timing_common.h"
#include "audio.h"

#include <verdigris/wobjectdefs.h>

#include <QMainWindow>
#include <QWidget>

#include <memory>

namespace gui::main_window {

using audio::output::AudioThreadHandle;
using timing::MaybeSequencerTime;

/// Everything exposed to other modules goes here. GUI widgets/etc. go in MainWindowPrivate.
class MainWindow : public QMainWindow
{
    W_OBJECT(MainWindow)

public:
    // MainWindow is a pure virtual class. All fields are declared in subclass.
    // impl
    static std::unique_ptr<MainWindow> make(
        doc::Document document, QWidget * parent = nullptr
    );

    static MainWindow & get_instance();

    MainWindow(QWidget *parent = nullptr);
    virtual void _() = 0;
    virtual ~MainWindow();

    virtual std::optional<AudioThreadHandle> const & audio_handle() = 0;

signals:
    void gui_refresh(MaybeSequencerTime maybe_seq_time)
        W_SIGNAL(gui_refresh, (MaybeSequencerTime), maybe_seq_time)

public slots:
    virtual void restart_audio_thread() = 0;
    W_SLOT(restart_audio_thread)
};


}

#endif // MAINWINDOW_H
