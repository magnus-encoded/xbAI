// pal/gdk-desktop/GdkInput.h
// IInput implementation for the gdk-desktop / dev-PC harness.
// There is no physical gamepad on the dev PC.  This implementation fires the
// activate callback exactly once, immediately after set_activate_handler() is
// called, so the model auto-starts in headless / CI use.  On the Xbox shell this
// seam would be replaced with a real GameInput gamepad poll.
#pragma once

#include "pal/IInput.h"

namespace xbai {

class GdkInput : public IInput {
public:
    GdkInput() = default;
    ~GdkInput() override = default;

    // Store the handler and fire it once immediately (headless auto-activate).
    void set_activate_handler(const std::function<void()>& on_activate) override;
};

}  // namespace xbai
