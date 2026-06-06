// core/pal/ILifecycle.h
// PAL seam: process lifecycle + platform PLM (Process Lifetime Management) hooks.
// The platform shell drives these; core reacts (e.g. pause the decode worker on
// suspend, resume on resume). Plain C++ only.
//
// === FROZEN by P0 (2026-06-06) === see types.h for the freeze rule.

#pragma once

#include <functional>

namespace xbai {

// core registers these callbacks; the platform invokes them on PLM transitions.
// All are optional (may be null). Invoked on the platform event-pump thread.
struct LifecycleCallbacks {
    std::function<void()> on_suspend;    // app is being suspended (PLM)
    std::function<void()> on_resume;     // app resumed from suspend
    std::function<void()> on_constrain;  // resources constrained (Xbox)
    std::function<void()> on_unconstrain;
};

// Owns process start/stop and routes platform PLM events to core.
class ILifecycle {
public:
    virtual ~ILifecycle() = default;

    // Wire up core's reactions to platform lifecycle transitions.
    virtual void set_callbacks(const LifecycleCallbacks& callbacks) = 0;

    // Begin the platform run/event loop. Blocks until the app is told to exit.
    virtual void start() = 0;

    // Request an orderly shutdown. Safe to call from another thread.
    virtual void stop() = 0;
};

}  // namespace xbai
