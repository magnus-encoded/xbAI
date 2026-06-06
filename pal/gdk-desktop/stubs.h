// pal/gdk-desktop/stubs.h
// P0 placeholder implementations of the non-socket PAL seams, just enough to
// compile/link the walking skeleton. Each is filled by a later pal-gdkdesktop-*
// task. No GDK/WinRT pulled in here yet (kept minimal for the skeleton build).
#pragma once

#include "pal/IDashboard.h"
#include "pal/IDownloader.h"
#include "pal/IInput.h"
#include "pal/ILifecycle.h"

#include <cstdio>

namespace xbai {

// Win32 lifecycle stub: no PLM transitions on plain desktop; start() returns
// immediately so the skeleton drives the socket loop itself.
class GdkLifecycle : public ILifecycle {
public:
    void set_callbacks(const LifecycleCallbacks&) override {}
    void start() override {}
    void stop() override {}
};

// GdkModelStore: real implementation in GdkModelStore.h / GdkModelStore.cpp.

// Downloader stub: reports start failure (no transfer backend wired yet).
class GdkDownloader : public IDownloader {
public:
    bool start(const std::string&, const std::vector<DownloadItem>&,
               const ProgressCallback&, const DownloadCallback& on_done) override {
        if (on_done) on_done(false, "GdkDownloader stub: not implemented (P0)");
        return false;
    }
    void cancel() override {}
};

// Input stub: no gamepad polling on the dev-PC harness yet.
class GdkInput : public IInput {
public:
    void set_activate_handler(const std::function<void()>&) override {}
};

// Dashboard stub: prints status to stdout (no UI surface in the skeleton).
class GdkDashboard : public IDashboard {
public:
    void set_status(const DashboardStatus& s) override {
        std::printf("[dashboard] state=%d endpoint=%s tok/s=%.2f queue=%d %s\n",
                    static_cast<int>(s.state), s.endpoint_url.c_str(),
                    s.tokens_per_second, s.queue_depth, s.message.c_str());
    }
};

}  // namespace xbai
