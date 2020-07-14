#include "app.h"
#include "win32_fonts.h"

#include <verdigris/wobjectimpl.h>

namespace gui::app {

W_OBJECT_IMPL(GuiApp)

SetFont::SetFont() {
    win32_set_font();
}

}
