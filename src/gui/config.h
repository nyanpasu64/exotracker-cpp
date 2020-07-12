#pragma once

#include <Qt>

namespace gui::config {

// # Shortcuts
// It's UB to cast (modifier | Qt::Key) to Qt::Key, because Qt::Key is unsized.
using KeyInt = int;

constexpr int chord(int modifier, KeyInt key) {
    return static_cast<Qt::Key>(modifier | int(key));
}

struct PatternShortcuts {
    constexpr static Qt::Key up{Qt::Key_Up};
    constexpr static Qt::Key down{Qt::Key_Down};

    KeyInt prev_beat{chord(Qt::CTRL, Qt::Key_Up)};
    KeyInt next_beat{chord(Qt::CTRL, Qt::Key_Down)};

    KeyInt prev_event{chord(Qt::CTRL | Qt::ALT, Qt::Key_Up)};
    KeyInt next_event{chord(Qt::CTRL | Qt::ALT, Qt::Key_Down)};

    KeyInt scroll_prev{Qt::Key_PageUp};
    KeyInt scroll_next{Qt::Key_PageDown};

    KeyInt prev_pattern{chord(Qt::CTRL, Qt::Key_PageUp)};
    KeyInt next_pattern{chord(Qt::CTRL, Qt::Key_PageDown)};

    // TODO nudge_prev/next via alt+up/down

    constexpr static Qt::Key left{Qt::Key_Left};
    constexpr static Qt::Key right{Qt::Key_Right};

    KeyInt scroll_left{chord(Qt::ALT, Qt::Key_Left)};
    KeyInt scroll_right{chord(Qt::ALT, Qt::Key_Right)};

    constexpr static Qt::Key delete_key{Qt::Key_Delete};
    constexpr static Qt::Key dummy_note{Qt::Key_Z};
};

/// Set via dialog. Written to disk when dialog applied or closed.
/// Stored in the GuiApp class.
struct Options {
    PatternShortcuts pattern_shortcuts;
};

// Persistent application fields are stored directly in GuiApp.
// Non-persistent and per-document state is stored in MainWindow.

/*
would be nice if editing one struct in config.h
didn't dirty files depending on a different struct in config.h...
rust's query-based compiler rearchitecture aims to achieve that.
does ccache achieve it?
*/

}
