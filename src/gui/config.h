#pragma once

#include "gui/config/cursor.h"
#include "gui/lib/color.h"
#include "doc/accidental_common.h"

#include <qkeycode/qkeycode.h>

#include <QColor>
#include <QFont>

#include <array>

namespace gui::config {
#ifndef gui_config_INTERNAL
#define gui_config_INTERNAL private
#endif

using doc::accidental::AccidentalMode;

inline namespace keys {
    // # Shortcuts
    // It's UB to cast (modifier | Qt::Key) to Qt::Key, because Qt::Key is unsized.
    using KeyInt = int;

    constexpr int chord(int modifier, KeyInt key) {
        return static_cast<Qt::Key>(modifier | int(key));
    }

    struct GlobalKeys {
        KeyInt play_pause{Qt::Key_Return};
        KeyInt play_from_row{Qt::Key_Apostrophe};
    };

    // Allow a few notes of the following octave. Match 0CC's behavior.
    static constexpr size_t NOTES_PER_ROW = 17;

    using KeyboardRow = std::array<qkeycode::KeyCode, NOTES_PER_ROW>;

    KeyboardRow get_octave_0();
    KeyboardRow get_octave_1();

    struct PatternKeys {
        constexpr static Qt::Key up{Qt::Key_Up};
        constexpr static Qt::Key down{Qt::Key_Down};

        KeyInt prev_beat{chord(Qt::CTRL, Qt::Key_Up)};
        KeyInt next_beat{chord(Qt::CTRL, Qt::Key_Down)};

        KeyInt prev_event{chord(Qt::CTRL | Qt::ALT, Qt::Key_Up)};
        KeyInt next_event{chord(Qt::CTRL | Qt::ALT, Qt::Key_Down)};

        KeyInt scroll_prev{Qt::Key_PageUp};
        KeyInt scroll_next{Qt::Key_PageDown};

        KeyInt top{Qt::Key_Home};
        KeyInt bottom{Qt::Key_End};

        KeyInt prev_pattern{chord(Qt::CTRL, Qt::Key_PageUp)};
        KeyInt next_pattern{chord(Qt::CTRL, Qt::Key_PageDown)};

        // TODO nudge_prev/next via alt+up/down

        constexpr static Qt::Key left{Qt::Key_Left};
        constexpr static Qt::Key right{Qt::Key_Right};

        KeyInt scroll_left{chord(Qt::ALT, Qt::Key_Left)};
        KeyInt scroll_right{chord(Qt::ALT, Qt::Key_Right)};

        constexpr static Qt::Key escape{Qt::Key_Escape};
        constexpr static Qt::Key toggle_edit{Qt::Key_Space};
        constexpr static Qt::Key delete_key{Qt::Key_Delete};
        KeyInt note_cut{Qt::Key_QuoteLeft};  // backtick
        // TODO switch to QKeySequence::SelectAll?
        KeyInt select_all{chord(Qt::CTRL, Qt::Key_A)};
        KeyInt selection_padding{chord(Qt::SHIFT, Qt::Key_Space)};

        std::array<KeyboardRow, 2> piano_keys{get_octave_0(), get_octave_1()};
    };

    using cursor::MovementConfig;
}

inline namespace visual {
    constexpr QColor BLACK{0, 0, 0};

    static constexpr QColor gray(int value) {
        return QColor{value, value, value};
    }

    static constexpr QColor gray_alpha(int value, int alpha) {
        return QColor{value, value, value, alpha};
    }

    struct FontTweaks {
        int width_adjust;

        /// To move text down, increase pixels_above_text and decrease pixels_below_text.
        int pixels_above_text;
        int pixels_below_text;
    };

    /// Overall colors (not different in focused/unfocused patterns).
    /// Stored in PatternAppearance fields.
    #define OVERALL_COLORS(X) \
        X(overall_bg) \
        X(base_subcolumn_bg) \
        X(channel_divider) \
        X(cursor_row) \
        X(cursor_row_edit) \
        X(cell)

    /// Colors which are dimmed in inactive patterns.
    /// Stored in PatternAppearance fields.
    #define PATTERN_COLORS(X) \
        X(gridline_beat) \
        X(gridline_non_beat) \
        X(select_bg) \
        X(select_border) \
        X(block_handle) \
        X(note_line_beat) \
        X(note_line_non_beat) \
        X(note_line_fractional) \
        X(instrument) \
        X(volume) \
        X(effect) \

    /// Subcolumn types used to parameterize background/divider methods.
    /// Not stored directly in PatternAppearance, but computed from other fields.
    /// Dimmed in inactive patterns.
    #define SUBCOLUMNS(X) \
        X(note) \
        X(instrument) \
        X(volume) \
        X(effect)


    /// Only used internally in PatternAppearance.
    enum class PatternColor {
        #define X(COLOR)  COLOR,
        PATTERN_COLORS(X)
        #undef X
    };

    /// Only used internally in PatternAppearance.
    enum class SubColumn {
        #define X(SUBCOLUMN)  SUBCOLUMN,
        SUBCOLUMNS(X)
        #undef X
    };


    class PatternAppearance {
    public:
        #define X(COLOR)  QColor COLOR;
        OVERALL_COLORS(X)
        #undef X

    gui_config_INTERNAL:
        #define X(COLOR)  QColor _##COLOR;
        PATTERN_COLORS(X)
        #undef X

        // All blending is conducted in approximate linear light (assuming gamma=2).
        // This differs from gamma/RGB blending!

        /// How opaquely to draw cells at a different grid index (time).
        /// At 0, unfocused patterns have the same color as the background.
        /// At 1, unfocused patterns have the same color as focused grid cells.
        qreal _unfocused_brightness;
        // TODO early-exit when drawing inactive patterns, if _unfocused_brightness = 0.
        // But foreach_grid(find_selection) cannot do this.

        /// How much to blend subcolumn colors into subcolumn backgrounds.
        /// At 0, subcolumn backgrounds have color "base_subcolumn_bg".
        /// At 1, subcolumn backgrounds have the same color as foreground text.
        qreal _subcolumn_bg_colorize;

        /// How bright to make subcolumn dividers.
        /// At 0, dividers have the same color as the subcolumn background.
        /// At 1, dividers have the same color as foreground text.
        qreal _subcolumn_divider_colorize;

    public:
        /// Cursor row color gradient.
        int cursor_top_alpha;
        int cursor_bottom_alpha;

        /// Cursor cell color gradient.
        int cell_top_alpha;
        int cell_bottom_alpha;


        /// Fonts to use.
        /// Initialized in default_appearance().
        QFont pattern_font;

        FontTweaks font_tweaks;

        // impl
    private:
        QColor get_color(PatternColor color_type, bool focused) const;
        QColor get_subcolumn_bg(SubColumn subcolumn, bool focused) const;
        QColor get_subcolumn_divider(SubColumn subcolumn, bool focused) const;

    public:
        #define X(COLOR) \
            inline QColor COLOR(bool focused) const { \
                return get_color(PatternColor::COLOR, focused); \
            }
        PATTERN_COLORS(X)
        #undef X

        QColor block_handle_border(bool focused) const;

        #define X(SUBCOLUMN) \
            inline QColor SUBCOLUMN##_bg(bool focused) const { \
                return get_subcolumn_bg(SubColumn::SUBCOLUMN, focused); \
            } \
            inline QColor SUBCOLUMN##_divider(bool focused) const { \
                return get_subcolumn_divider(SubColumn::SUBCOLUMN, focused); \
            }
        SUBCOLUMNS(X)
        #undef X
    };

    PatternAppearance default_appearance();

    struct NoteNameConfig {
        /// MIDI pitch 0 lies in this octave.
        int gui_bottom_octave;

        QChar sharp_char;
        QChar flat_char;
        QChar natural_char;
    };
}

/// Set via dialog. Written to disk when dialog applied or closed.
/// Stored in the GuiApp class.
struct Options {
    GlobalKeys global_keys;
    PatternKeys pattern_keys;
    MovementConfig move_cfg;

    PatternAppearance visual = default_appearance();

    NoteNameConfig note_names {
        .gui_bottom_octave = -1,
        .sharp_char = '#',
        .flat_char = 'b',
        .natural_char = 0xB7,
    };

    AccidentalMode default_accidental_mode = AccidentalMode::Sharp;
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
