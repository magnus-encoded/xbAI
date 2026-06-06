// pal/gdk-desktop/GdkSocketListener.cpp
// Winsock TCP listener implementing core's ISocketListener. P0 walking skeleton:
// binds a port, runs the accept loop, hands each connection to core as an
// IConnection byte stream.

#include "GdkSocketListener.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>

#pragma comment(lib, "Ws2_32.lib")

namespace xbai {

namespace {

// A live accepted socket presented to core as a byte stream.
class WinsockConnection : public IConnection {
public:
    explicit WinsockConnection(SOCKET s, std::string peer) : sock_(s), peer_(std::move(peer)) {}
    ~WinsockConnection() override {
        if (sock_ != INVALID_SOCKET) {
            shutdown(sock_, SD_BOTH);
            closesocket(sock_);
        }
    }

    int read(char* buf, size_t max) override {
        int n = recv(sock_, buf, static_cast<int>(max), 0);
        return n;  // >0 bytes, 0 orderly close, SOCKET_ERROR(-1) on error
    }

    int write(const char* buf, size_t len) override {
        int n = send(sock_, buf, static_cast<int>(len), 0);
        return n;
    }

    std::string peer() const override { return peer_; }

private:
    SOCKET sock_;
    std::string peer_;
};

}  // namespace

GdkSocketListener::GdkSocketListener() : listen_socket_(INVALID_SOCKET) {}

GdkSocketListener::~GdkSocketListener() {
    if (listen_socket_ != INVALID_SOCKET) {
        closesocket(static_cast<SOCKET>(listen_socket_));
    }
    WSACleanup();
}

bool GdkSocketListener::listen(uint16_t port, std::string& error) {
    WSADATA wsa;
    int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) {
        error = "WSAStartup failed: " + std::to_string(rc);
        return false;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        error = "socket() failed: " + std::to_string(WSAGetLastError());
        return false;
    }

    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        error = "bind() failed on port " + std::to_string(port) + ": " +
                std::to_string(WSAGetLastError());
        closesocket(s);
        return false;
    }

    if (::listen(s, SOMAXCONN) == SOCKET_ERROR) {
        error = "listen() failed: " + std::to_string(WSAGetLastError());
        closesocket(s);
        return false;
    }

    // Resolve the actual bound port (handles port 0 -> ephemeral).
    sockaddr_in bound{};
    int blen = sizeof(bound);
    if (getsockname(s, reinterpret_cast<sockaddr*>(&bound), &blen) == 0) {
        port_ = ntohs(bound.sin_port);
    } else {
        port_ = port;
    }

    listen_socket_ = static_cast<unsigned long long>(s);
    return true;
}

void GdkSocketListener::run(const ConnectionHandler& on_connection) {
    if (listen_socket_ == INVALID_SOCKET) {
        return;
    }
    SOCKET ls = static_cast<SOCKET>(listen_socket_);
    running_ = true;

    while (running_) {
        sockaddr_in peer{};
        int plen = sizeof(peer);
        SOCKET client = accept(ls, reinterpret_cast<sockaddr*>(&peer), &plen);
        if (client == INVALID_SOCKET) {
            if (!running_) break;  // stop() closed the listener -> clean exit
            continue;
        }

        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        std::string peer_str = std::string(ip) + ":" + std::to_string(ntohs(peer.sin_port));

        on_connection(std::make_unique<WinsockConnection>(client, std::move(peer_str)));
    }
}

void GdkSocketListener::stop() {
    running_ = false;
    if (listen_socket_ != INVALID_SOCKET) {
        // Closing the listen socket unblocks accept().
        closesocket(static_cast<SOCKET>(listen_socket_));
        listen_socket_ = INVALID_SOCKET;
    }
}

uint16_t GdkSocketListener::port() const {
    return port_;
}

}  // namespace xbai
