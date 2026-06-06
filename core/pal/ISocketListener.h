// core/pal/ISocketListener.h
// PAL seam: accept inbound TCP and hand core a per-connection byte stream.
// The platform owns the accept loop (ADR-0006 §2); core owns the worker threads
// and the HTTP/server logic that runs over each connection. Plain C++ only.
//
// === FROZEN by P0 (2026-06-06) === see types.h for the freeze rule.

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace xbai {

// A live, accepted TCP connection. Blocking, byte-oriented. core/server reads a
// request off it and writes the response. Closing/destroying ends the connection.
class IConnection {
public:
    virtual ~IConnection() = default;

    // Read up to `max` bytes into `buf`. Returns bytes read (>0), 0 on orderly
    // close, or -1 on error. Blocks until data is available.
    virtual int read(char* buf, size_t max) = 0;

    // Write exactly `len` bytes. Returns bytes written, or -1 on error.
    virtual int write(const char* buf, size_t len) = 0;

    // Peer address, for logging ("192.168.1.10:51234"). Best-effort.
    virtual std::string peer() const = 0;
};

// Called on the accept loop for each new connection, with ownership transferred.
// The handler typically hands the connection to a core worker and returns.
using ConnectionHandler = std::function<void(std::unique_ptr<IConnection>)>;

// Binds a TCP port and runs the accept loop. One per server instance.
class ISocketListener {
public:
    virtual ~ISocketListener() = default;

    // Bind `port` on all interfaces. Returns false / sets `error` on failure.
    virtual bool listen(uint16_t port, std::string& error) = 0;

    // Run the accept loop, invoking `on_connection` per accepted socket. Blocks
    // until stop() is called. Returns when the loop exits.
    virtual void run(const ConnectionHandler& on_connection) = 0;

    // Ask run() to return. Safe to call from another thread.
    virtual void stop() = 0;

    // The bound port (resolves port 0 to the OS-chosen ephemeral port).
    virtual uint16_t port() const = 0;
};

}  // namespace xbai
