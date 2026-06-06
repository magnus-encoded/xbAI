// core/server/HttpServer.h
// HTTP/1.1 server over ISocketListener.  Routes POST /v1/chat/completions to
// an IInferenceEngine.  Supports single JSON response and SSE streaming.
// Plain C++20 only -- ZERO WinRT/GDK/Windows.h (ADR-0006 SS6).

#pragma once

#include <string>

#include "pal/IInferenceEngine.h"
#include "pal/ISocketListener.h"

namespace xbai {

// Parsed HTTP/1.1 request line + headers (just enough for routing).
struct HttpRequest {
    std::string method;         // "GET", "POST", ...
    std::string path;           // "/v1/chat/completions"
    int         content_length = 0;
    std::string content_type;
    bool        parse_ok = false; // false if the request line was malformed
};

class HttpServer {
public:
    // Injected references -- HttpServer does NOT take ownership.
    HttpServer(ISocketListener& listener, IInferenceEngine& engine);

    // Block and run the accept loop.  Returns when listener.stop() is called.
    void serve();

private:
    // Handle one accepted connection synchronously.
    void handle_connection(IConnection& conn);

    // Read the full HTTP/1.1 request line + headers from conn.
    // Returns a populated HttpRequest (parse_ok==false on bad request).
    HttpRequest read_request_head(IConnection& conn);

    // Read exactly `length` bytes from the connection into `body`.
    // Returns false on connection error.
    bool read_body(IConnection& conn, int length, std::string& body);

    // Route: POST /v1/chat/completions
    void handle_chat(IConnection& conn, const std::string& body);

    // Write a complete HTTP/1.1 response with the given status and body.
    void send_response(IConnection& conn,
                       int status_code,
                       const std::string& status_text,
                       const std::string& content_type,
                       const std::string& body);

    // Write a 400 error JSON response.
    void send_error(IConnection& conn, const std::string& message);

    // Write a 404 response.
    void send_404(IConnection& conn);

    // Write one SSE data frame (chunked transfer encoding).
    // chunk_data is the raw text that follows "data: ".
    bool write_sse_chunk(IConnection& conn, const std::string& data_line);

    // Write the chunked-TE terminator (zero-length chunk).
    bool write_chunked_terminator(IConnection& conn);

    ISocketListener& listener_;
    IInferenceEngine& engine_;
};

} // namespace xbai
