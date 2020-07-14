#pragma once

#include "gui/lib/color.h"

#include <QColor>
#include <QFont>

namespace gui::config {

inline namespace keys {
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
}

inline namespace visual {
    using gui::lib::color::lerp_colors;

    constexpr QColor BLACK{0, 0, 0};
    constexpr qreal BG_COLORIZE = 0.05;

    static constexpr QColor gray(int value) {
        return QColor{value, value, value};
    }

    static constexpr QColor gray_alpha(int value, int alpha) {
        return QColor{value, value, value, alpha};
    }

    struct FontTweaks {
        int width_adjust = 0;

        // To move text down, increase pixels_above_text and decrease pixels_below_text.
        int pixels_above_text = 1;
        int pixels_below_text = -1;
    };

    struct PatternAppearance {
        QColor overall_bg = gray(48);

        /// Vertical line to the right of each channel.
        QColor channel_divider = gray(160);

        /// Background gridline color.
        QColor gridline_beat = gray(128);
        QColor gridline_non_beat = gray(80);

        /// Cursor color.
        QColor cursor_row = gray(240);
        int cursor_top_alpha = 48;
        int cursor_bottom_alpha = 0;

        QColor cell{255, 255, 96};
        int cell_top_alpha = 96;
        int cell_bottom_alpha = 96;

        /// Foreground line color, also used as note text color.
        QColor note_line_beat{255, 255, 96};
        QColor note_line_non_beat{0, 255, 0};
        QColor note_line_fractional{0, 224, 255};
        QColor note_bg = lerp_colors(BLACK, note_line_beat, BG_COLORIZE);

        /// Instrument text color.
        QColor instrument{128, 255, 128};
        QColor instrument_bg = lerp_colors(BLACK, instrument, BG_COLORIZE);

        // Volume text color.
        QColor volume{0, 255, 255};
        QColor volume_bg = lerp_colors(BLACK, volume, BG_COLORIZE);

        // Effect name color.
        QColor effect{255, 128, 128};
        QColor effect_bg = lerp_colors(BLACK, effect, BG_COLORIZE);

        /// How bright to make subcolumn dividers.
        /// At 0, dividers are the same color as the background.
        /// At 1, dividers are the same color as foreground text.
        qreal subcolumn_divider_blend = 0.15;

        /// Fonts to use.
        /// Initialized in default_appearance().
        QFont pattern_font;

        FontTweaks font_tweaks;
    };

    PatternAppearance default_appearance();
}

/// Set via dialog. Written to disk when dialog applied or closed.
/// Stored in the GuiApp class.
struct Options {
    PatternShortcuts pattern_shortcuts;
    PatternAppearance visual = default_appearance();
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
