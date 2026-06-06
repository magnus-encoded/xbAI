// pal/gdk-desktop/GdkInput.cpp
#include "GdkInput.h"

namespace xbai {

void GdkInput::set_activate_handler(const std::function<void()>& on_activate) {
    // Dev-PC: no physical gamepad.  Fire the activate intent once immediately so
    // the model auto-starts without requiring user interaction.
    if (on_activate) {
        on_activate();
    }
}

}  // namespace xbai
