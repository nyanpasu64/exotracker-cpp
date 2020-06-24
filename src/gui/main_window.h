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

struct PatternAndBeat {
    doc::SeqEntryIndex seq_entry_index = 0;
    doc::BeatFraction beat = 0;
};

/// Everything exposed to other modules goes here. GUI widgets/etc. go in MainWindowPrivate.
class MainWindow : public QMainWindow
{
    W_OBJECT(MainWindow)

public:
    // Just make it a grab bag of fields for now.
    // TODO add "cursor_moved" signal.

    // TODO encapsulate selection begin (x, y) and cursor (x, y)
    // into a Selection class.
    // set_cursor() and select_to()?
    // set_cursor(bool select)?

    // TODO cursor_x
    PatternAndBeat _cursor_y;

    PatternAndBeat _select_begin_y;

public:
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
