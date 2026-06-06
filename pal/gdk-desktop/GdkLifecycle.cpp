// pal/gdk-desktop/GdkLifecycle.cpp
#include "GdkLifecycle.h"

#include <thread>
#include <chrono>

namespace xbai {

void GdkLifecycle::set_callbacks(const LifecycleCallbacks& callbacks) {
    callbacks_ = callbacks;
}

void GdkLifecycle::start() {
    exit_requested_.store(false, std::memory_order_relaxed);
    // Spin-sleep until stop() is called.  The dev-PC harness has no Win32 message
    // pump; this keeps the thread alive while the socket accept loop runs on the
    // same thread (or a worker).  Sleep prevents a 100% CPU busy-wait.
    while (!exit_requested_.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void GdkLifecycle::stop() {
    exit_requested_.store(true, std::memory_order_release);
}

}  // namespace xbai
