// pal/gdk-desktop/GdkSocketListener.cpp
// Winsock TCP listener implementing core's ISocketListener.
// Full implementation: partial-write loop, TCP_NODELAY, atomic stop flag.

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

    // Single recv() is correct: the interface contract says "blocks until data is
    // available" and returns however many bytes arrived. Callers loop themselves.
    // Returns >0 (bytes read), 0 (orderly close), or -1 (error).
    int read(char* buf, size_t max) override {
        int n = recv(sock_, buf, static_cast<int>(max), 0);
        // SOCKET_ERROR == -1 on Windows; matches the IConnection contract.
        return n;
    }

    // Write exactly `len` bytes. Loops on partial sends (critical for SSE chunks).
    // Returns `len` on full success, or -1 on error.
    int write(const char* buf, size_t len) override {
        size_t sent = 0;
        while (sent < len) {
            int n = send(sock_, buf + sent, static_cast<int>(len - sent), 0);
            if (n == SOCKET_ERROR) {
                return -1;
            }
            sent += static_cast<size_t>(n);
        }
        return static_cast<int>(sent);
    }

    std::string peer() const override { return peer_; }

private:
    SOCKET sock_;
    std::string peer_;
};

}  // namespace

GdkSocketListener::GdkSocketListener() : listen_socket_(INVALID_SOCKET) {}

GdkSocketListener::~GdkSocketListener() {
    // stop() may have already closed the socket; only close if still open.
    if (listen_socket_ != INVALID_SOCKET) {
        closesocket(static_cast<SOCKET>(listen_socket_));
        listen_socket_ = INVALID_SOCKET;
    }
    if (wsa_started_) {
        WSACleanup();
    }
}

bool GdkSocketListener::listen(uint16_t port, std::string& error) {
    WSADATA wsa;
    int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) {
        error = "WSAStartup failed: " + std::to_string(rc);
        return false;
    }
    wsa_started_ = true;

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

    // Accept loop. For the single-flight model, on_connection blocks until the
    // request (including any SSE token stream) is complete. That is intentional:
    // the inference queue serializes work, so there is never a second concurrent
    // request to service. The listener socket stays open; accept() has no timeout.
    while (running_) {
        sockaddr_in peer_addr{};
        int plen = sizeof(peer_addr);
        SOCKET client = accept(ls, reinterpret_cast<sockaddr*>(&peer_addr), &plen);
        if (client == INVALID_SOCKET) {
            if (!running_) break;  // stop() closed the listener -> clean exit
            // Transient error; keep looping.
            continue;
        }

        // Disable Nagle: flush each SSE chunk immediately without 200 ms delay.
        BOOL no_delay = TRUE;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&no_delay), sizeof(no_delay));

        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &peer_addr.sin_addr, ip, sizeof(ip));
        std::string peer_str = std::string(ip) + ":" + std::to_string(ntohs(peer_addr.sin_port));

        on_connection(std::make_unique<WinsockConnection>(client, std::move(peer_str)));
    }
}

void GdkSocketListener::stop() {
    running_ = false;
    if (listen_socket_ != INVALID_SOCKET) {
        // Closing the listen socket unblocks the blocking accept() call in run().
        // run() checks !running_ after accept() returns INVALID_SOCKET and exits.
        closesocket(static_cast<SOCKET>(listen_socket_));
        listen_socket_ = INVALID_SOCKET;
    }
}

uint16_t GdkSocketListener::port() const {
    return port_;
}

}  // namespace xbai
