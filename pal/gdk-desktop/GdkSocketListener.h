// pal/gdk-desktop/GdkSocketListener.h
// Winsock implementation of core's ISocketListener seam for the GameCore Win32
// (Gaming.Desktop.x64) backend / dev-PC harness.
#pragma once

#include "pal/ISocketListener.h"

namespace xbai {

class GdkSocketListener : public ISocketListener {
public:
    GdkSocketListener();
    ~GdkSocketListener() override;

    bool listen(uint16_t port, std::string& error) override;
    void run(const ConnectionHandler& on_connection) override;
    void stop() override;
    uint16_t port() const override;

private:
    // Opaque to keep Winsock headers (winsock2.h) out of this header so callers
    // need not pull in <windows.h>. Defined in the .cpp.
    unsigned long long listen_socket_;  // SOCKET; INVALID_SOCKET when closed
    uint16_t port_ = 0;
    volatile bool running_ = false;
};

}  // namespace xbai
