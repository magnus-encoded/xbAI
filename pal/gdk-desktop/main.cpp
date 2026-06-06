// pal/gdk-desktop/main.cpp
// Entry point for the GameCore Win32 (Gaming.Desktop.x64) backend / dev-PC
// harness. P0 walking skeleton: init runtime, bind a Winsock listener, accept
// one connection, answer a canned HTTP 200, exit.
//
// GDK note: the real GameCore entry is WinMain + XGameRuntimeInitialize() /
// XGameRuntimeUninitialize(). The public PC GDK is not installed on this dev box
// (no Gaming.Desktop.x64 platform / no XGameRuntime.h), so this builds against a
// plain main() guarded by XBAI_HAVE_GDK. Flip the define (and link xgameruntime)
// once the GDK is installed; the Winsock listener — the load-bearing part — is
// identical either way.

#include "GdkModelStore.h"
#include "GdkSocketListener.h"
#include "stubs.h"

#include "core.h"

#include <cstdio>
#include <cstdlib>
#include <string>

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

// Drain the request line (best-effort) and write a canned HTTP/1.1 200.
// This is the skeleton stand-in for core/server; it proves the seam end-to-end.
void serve_canned_200(xbai::IConnection& conn) {
    char buf[2048];
    conn.read(buf, sizeof(buf));  // consume the request; content ignored in P0

    static const char kResponse[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 2\r\n"
        "Connection: close\r\n"
        "\r\n"
        "OK";
    conn.write(kResponse, sizeof(kResponse) - 1);
}

int run(int argc, char** argv) {
    std::printf("xbai gdk-desktop skeleton — core_version=%d\n", xbai::core_version());

    const std::string models_root = pick_models_root(argc, argv);
    xbai::GdkModelStore model_store(models_root);
    std::printf("model store root: %s\n", models_root.c_str());

    const uint16_t want_port = pick_port(argc, argv);

    xbai::GdkSocketListener listener;
    std::string error;
    if (!listener.listen(want_port, error)) {
        std::fprintf(stderr, "listen failed: %s\n", error.c_str());
        return 1;
    }

    xbai::GdkDashboard dashboard;
    xbai::DashboardStatus status;
    status.state = xbai::ServerState::Ready;
    status.endpoint_url = "http://127.0.0.1:" + std::to_string(listener.port());
    dashboard.set_status(status);

    std::printf("listening on %s — accepting connections (Ctrl+C to quit)\n",
                status.endpoint_url.c_str());
    std::fflush(stdout);

    // Skeleton accept loop: answer 200 on every connection.
    listener.run([](std::unique_ptr<xbai::IConnection> conn) {
        serve_canned_200(*conn);
    });

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
