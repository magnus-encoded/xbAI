// core/pal/IInput.h
// PAL seam: report the "press-A" intent (focus + activate). On Xbox this is a
// GameInput gamepad poll; on desktop it can be a keypress or a no-op. core only
// needs to know "did the user activate the app?". Plain C++ only.
//
// === FROZEN by P0 (2026-06-06) === see types.h for the freeze rule.

#pragma once

#include <functional>

namespace xbai {

// Surfaces the single activation intent core cares about. The platform polls /
// pumps input on its own thread and fires the callback on a press-A (or the
// desktop equivalent).
class IInput {
public:
    virtual ~IInput() = default;

    // Register the handler invoked when the user signals "activate" (press A).
    virtual void set_activate_handler(const std::function<void()>& on_activate) = 0;
};

}  // namespace xbai
