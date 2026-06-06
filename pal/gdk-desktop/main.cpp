// pal/gdk-desktop/main.cpp
// Entry point for the GameCore Win32 (Gaming.Desktop.x64) backend / dev-PC
// harness.  Wave-1 shell: full topology wired -- GdkLifecycle, GdkSocketListener,
// GdkModelStore, GdkDownloader, GdkInput, GdkDashboard, StubInferenceEngine,
// InferenceQueue, HttpServer.
//
// GDK note: the real GameCore entry is WinMain + XGameRuntimeInitialize() /
// XGameRuntimeUninitialize(). The public PC GDK is not installed on this dev box
// (no Gaming.Desktop.x64 platform / no XGameRuntime.h), so this builds against a
// plain main() guarded by XBAI_HAVE_GDK. Flip the define (and link xgameruntime)
// once the GDK is installed; the Winsock listener -- the load-bearing part -- is
// identical either way.

#include "GdkDashboard.h"
#include "GdkDownloader.h"
#include "GdkInput.h"
#include "GdkLifecycle.h"
#include "GdkModelStore.h"
#include "GdkSocketListener.h"
#include "StubInferenceEngine.h"

#include "core.h"
#include "queue/InferenceQueue.h"
#include "server/HttpServer.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#ifdef XBAI_HAVE_GDK
#include <XGameRuntime.h>
#endif

namespace {

// Pick the models root: argv[2] > default_root().
std::string pick_models_root(int argc, char** argv) {
    if (argc > 2 && argv[2] && argv[2][0] != '\0') return argv[2];
    return xbai::GdkModelStore::default_root();
}

// Pick the listen port: argv[1] > XBAI_PORT env > default 8080.
uint16_t pick_port(int argc, char** argv) {
    if (argc > 1) {
        int p = std::atoi(argv[1]);
        if (p > 0 && p < 65536) return static_cast<uint16_t>(p);
    }
    if (const char* env = std::getenv("XBAI_PORT")) {
        int p = std::atoi(env);
        if (p > 0 && p < 65536) return static_cast<uint16_t>(p);
    }
    return 8080;
}

int run(int argc, char** argv) {
    std::printf("xbai gdk-desktop shell -- core_version=%d\n", xbai::core_version());

    // ---- Lifecycle -------------------------------------------------------
    xbai::GdkLifecycle lifecycle;

    // ---- Model store + downloader ----------------------------------------
    const std::string models_root = pick_models_root(argc, argv);
    xbai::GdkModelStore model_store(models_root);
    xbai::GdkDownloader downloader;
    std::printf("model store root: %s\n", models_root.c_str());

    // ---- Listener --------------------------------------------------------
    const uint16_t want_port = pick_port(argc, argv);
    xbai::GdkSocketListener listener;
    std::string error;
    if (!listener.listen(want_port, error)) {
        std::fprintf(stderr, "listen failed: %s\n", error.c_str());
        return 1;
    }

    // ---- Input -----------------------------------------------------------
    xbai::GdkInput input;
    // Auto-fires once immediately on dev-PC (no physical gamepad).
    input.set_activate_handler([]() {
        std::printf("[xbai] activate intent received (auto-fire on dev-PC)\n");
        std::fflush(stdout);
    });

    // ---- Dashboard -------------------------------------------------------
    xbai::GdkDashboard dashboard;
    xbai::DashboardStatus status;
    status.state        = xbai::ServerState::Ready;
    status.endpoint_url = "http://127.0.0.1:" + std::to_string(listener.port());
    dashboard.set_status(status);

    std::printf("listening on %s\n", status.endpoint_url.c_str());
    std::fflush(stdout);

    // ---- Inference: stub engine + serializing queue ----------------------
    xbai::StubInferenceEngine stub_engine;
    xbai::InferenceQueue queue(stub_engine);

    // ---- HTTP server -----------------------------------------------------
    xbai::HttpServer http_server(listener, queue);

    // Run the HTTP accept loop on a background thread so lifecycle.start() can
    // block the main thread (mirroring how a real Xbox shell would work).
    std::thread server_thread([&http_server]() {
        http_server.serve();
    });

    // Wire lifecycle callbacks (PLM transitions -- not fired on dev-PC, but
    // correctly set up for the Xbox shell).
    xbai::LifecycleCallbacks cbs;
    cbs.on_suspend    = [&queue]()  { /* pause decode if needed */ (void)queue; };
    cbs.on_resume     = [&queue]()  { /* resume decode */ (void)queue; };
    cbs.on_constrain  = []() {};
    cbs.on_unconstrain= []() {};
    lifecycle.set_callbacks(cbs);

    // Block until Ctrl+C / stop().  On dev-PC the process is killed by the user;
    // listener.stop() + lifecycle.stop() would be called from a signal handler or
    // a management thread in a production build.
    lifecycle.start();

    // Orderly shutdown.
    listener.stop();
    queue.shutdown();
    if (server_thread.joinable()) server_thread.join();

    return 0;
}

}  // namespace

#ifdef XBAI_HAVE_GDK

// GameCore entry. argv is not handed to WinMain; use the env var / default port.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    HRESULT hr = XGameRuntimeInitialize();
    if (FAILED(hr)) {
        std::fprintf(stderr, "XGameRuntimeInitialize failed: 0x%08lx\n", hr);
        return 1;
    }
    int rc = run(__argc, __argv);
    XGameRuntimeUninitialize();
    return rc;
}

#else

// Dev-PC fallback (GDK not installed): plain console entry.
int main(int argc, char** argv) {
    return run(argc, argv);
}

#endif
