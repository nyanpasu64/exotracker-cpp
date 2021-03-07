#define gui_config_INTERNAL public
#include "config.h"

#include <qkeycode/values.h>

#include <QApplication>

namespace gui::config {

inline namespace visual {
    PatternAppearance default_appearance() {
        return PatternAppearance {
            // # Overall colors (not different in focused/unfocused patterns).

            /// Pattern editor background. Used for ruler and block handle columns.
            .overall_bg = gray(38),

            /// Background for subcolumns. May be blended with text color
            /// (if _subcolumn_bg_colorize is nonzero).
            .base_subcolumn_bg = BLACK,

            /// Vertical line to the right of each channel.
            .channel_divider = gray(160),

            /// Cursor line+row color.
            .cursor_row = gray(240),
            .cursor_row_edit{255, 160, 160},

            /// Cursor cell color.
            .cell{255, 255, 96},

            // # Pattern colors.

            /// Background gridline color.
            // TODO add _gridline_measure
            ._gridline_beat = gray(128),
            ._gridline_non_beat = gray(80),

            /// Selection color.
            ._select_bg{134, 125, 242, 192},
            ._select_border{150, 146, 211},

            /// Block handle to the left of each channel.
            ._block_handle = gray(114),

            /// Foreground line color, also used as note text color.
            ._note_line_beat{255, 255, 96},
            ._note_line_non_beat{0, 255, 0},
            ._note_line_fractional{0, 224, 255},

            /// Instrument text color.
            ._instrument{128, 255, 128},

            /// Volume text color.
            ._volume{0, 255, 255},

            /// Effect name color.
            ._effect{255, 128, 128},

            // # Numeric values.

            /// Unfocused grid cells.
            ._unfocused_brightness = 0.4,
            /// Subcolumn bg/dividers.
            ._subcolumn_bg_colorize = 0.05,
            ._subcolumn_divider_colorize = 0.15,

            /// Cursor row color gradient.
            .cursor_top_alpha = 64,
            .cursor_bottom_alpha = 0,

            /// Cursor cell color gradient.
            .cell_top_alpha = 96,
            .cell_bottom_alpha = 96,

            .pattern_font = ({
                QFont out{"dejavu sans mono", 9};
                out.setStyleHint(QFont::TypeWriter);
                out;
            }),

            .font_tweaks = FontTweaks {
                .width_adjust = 0,
                .pixels_above_text = 1,
                .pixels_below_text = -1,
            },
        };
    }

    using gui::lib::color::lerp;
    using gui::lib::color::lerp_colors;

    /// Dim inactive patterns.
    inline QColor dim_unfocused(
        PatternAppearance const& self, QColor color, bool focused
    ) {
        if (!focused) {
            color = lerp_colors(self.overall_bg, color, self._unfocused_brightness);
        }
        return color;
    }

    static QColor get_color_raw(PatternAppearance const& visual, PatternColor color_type) {
        // If an invalid enum is passed in, return magenta.
        QColor out = {255, 0, 255};

        // please optimize this switch into an indexing operation
        switch (color_type) {
            #define X(COLOR)  case PatternColor::COLOR: out = visual._##COLOR; break;
            PATTERN_COLORS(X)
            #undef X
        }

        return out;
    }

    QColor PatternAppearance::get_color(PatternColor color_type, bool focused) const {
        QColor out = get_color_raw(*this, color_type);
        return dim_unfocused(*this, out, focused);
    }


    static QColor get_subcolumn_color_raw(
        PatternAppearance const& visual, SubColumn color_type
    ) {
        // If an invalid enum is passed in, return magenta.
        QColor out = {255, 0, 255};

        switch (color_type) {
            case SubColumn::note:
                // This case is not like the rest.
                out = visual._note_line_beat;
                break;
            case SubColumn::instrument:
                out = visual._instrument;
                break;
            case SubColumn::volume:
                out = visual._volume;
                break;
            case SubColumn::effect:
                out = visual._effect;
                break;
        }

        return out;
    }

    QColor PatternAppearance::get_subcolumn_bg(
        SubColumn subcolumn, bool focused
    ) const {
        // How bright to draw subcolumn backgrounds.
        auto bg_colorize = _subcolumn_bg_colorize;

        QColor fg = get_subcolumn_color_raw(*this, subcolumn);
        QColor bg = lerp_colors(base_subcolumn_bg, fg, bg_colorize);

        return dim_unfocused(*this, bg, focused);
    }

    QColor PatternAppearance::get_subcolumn_divider(
        SubColumn subcolumn, bool focused
    ) const {
        QColor fg = get_subcolumn_color_raw(*this, subcolumn);

        auto divider_colorize = ({
            auto bg_colorize = _subcolumn_bg_colorize;
            float fg_colorize = 1.;
            lerp(bg_colorize, fg_colorize, _subcolumn_divider_colorize);
        });
        QColor divider = lerp_colors(base_subcolumn_bg, fg, divider_colorize);

        return dim_unfocused(*this, divider, focused);
    }

    QColor PatternAppearance::block_handle_border(bool focused) const {
        QColor border_blend = _block_handle.value() >= overall_bg.value()
            ? config::gray(255)
            : config::gray(0);
        auto border = lerp_colors(_block_handle, border_blend, 0.4);

        return dim_unfocused(*this, border, focused);
    }
}

inline namespace keys {
    using qkeycode::KeyCode;

    KeyboardRow get_octave_0() {
        return KeyboardRow {
            KeyCode::US_Z,     // C
            KeyCode::US_S,     // C#
            KeyCode::US_X,     // D
            KeyCode::US_D,     // D#
            KeyCode::US_C,     // E
            KeyCode::US_V,     // F
            KeyCode::US_G,     // F#
            KeyCode::US_B,     // G
            KeyCode::US_H,     // G#
            KeyCode::US_N,     // A
            KeyCode::US_J,     // A#
            KeyCode::US_M,     // B
            KeyCode::COMMA,    // C
            KeyCode::US_L,     // C#
            KeyCode::PERIOD,   // D
            KeyCode::SEMICOLON,// D#
            KeyCode::SLASH,    // E
        };
    }

    KeyboardRow get_octave_1() {
        return KeyboardRow {
            KeyCode::US_Q,  // C
            KeyCode::DIGIT2,  // C#
            KeyCode::US_W,  // D
            KeyCode::DIGIT3,  // D#
            KeyCode::US_E,  // E
            KeyCode::US_R,  // F
            KeyCode::DIGIT5,  // F#
            KeyCode::US_T,  // G
            KeyCode::DIGIT6,  // G#
            KeyCode::US_Y,  // A
            KeyCode::DIGIT7,  // A#
            KeyCode::US_U,  // B
            KeyCode::US_I, // C
            KeyCode::DIGIT9, // C#
            KeyCode::US_O, // D
            KeyCode::DIGIT0, // D#
            KeyCode::US_P, // E
        };
    }
}

}
