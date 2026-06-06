// pal/gdk-desktop/GdkLifecycle.h
// ILifecycle implementation for the gdk-desktop / dev-PC harness.
// PLM is trivial on plain Windows desktop: there are no OS-managed suspend /
// constrain transitions.  The callbacks are stored and wired correctly so the
// interface contract is met; on the dev PC they will never fire.
// start() blocks (spinning on an atomic flag) until stop() is called -- this
// lets the caller treat it as a "run until asked to exit" primitive without
// requiring a real Win32 message pump.
#pragma once

#include "pal/ILifecycle.h"

#include <atomic>

namespace xbai {

class GdkLifecycle : public ILifecycle {
public:
    GdkLifecycle() = default;
    ~GdkLifecycle() override = default;

    // ILifecycle -------------------------------------------------------

    // Store the PLM callbacks.  On dev-PC they will never be invoked; they are
    // kept so a future Xbox shell can fire them without changing the interface.
    void set_callbacks(const LifecycleCallbacks& callbacks) override;

    // Block until stop() is called (no real event pump needed on desktop).
    void start() override;

    // Signal start() to return.  Safe to call from another thread.
    void stop() override;

private:
    LifecycleCallbacks callbacks_;
    std::atomic<bool>  exit_requested_{false};
};

}  // namespace xbai
