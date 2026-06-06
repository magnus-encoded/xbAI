// core/server/HttpServer.cpp
// HTTP/1.1 server: request parsing, routing, response writing.
// Plain C++20 only -- ZERO WinRT/GDK/Windows.h (ADR-0006 SS6).

#include "HttpServer.h"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "JsonUtils.h"
#include "pal/IInferenceEngine.h"
#include "pal/ISocketListener.h"
#include "pal/types.h"

namespace xbai {

// ---------------------------------------------------------------------------
// Internal helpers (file-local)
// ---------------------------------------------------------------------------

namespace {

// Case-insensitive ASCII comparison for header field names.
static bool iequal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char ac = (a[i] >= 'A' && a[i] <= 'Z') ? (a[i] + 32) : a[i];
        char bc = (b[i] >= 'A' && b[i] <= 'Z') ? (b[i] + 32) : b[i];
        if (ac != bc) return false;
    }
    return true;
}

// Trim leading and trailing ASCII whitespace/CR from a string (in-place).
static void trim(std::string& s) {
    // trailing
    while (!s.empty() &&
           (s.back() == ' ' || s.back() == '\t' ||
            s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    // leading
    size_t start = 0;
    while (start < s.size() &&
           (s[start] == ' ' || s[start] == '\t')) ++start;
    if (start) s.erase(0, start);
}

// Write all bytes to an IConnection, returning false on failure.
static bool write_all(IConnection& conn, const std::string& data) {
    const char* p = data.data();
    int remaining = static_cast<int>(data.size());
    while (remaining > 0) {
        int sent = conn.write(p, static_cast<size_t>(remaining));
        if (sent <= 0) return false;
        p         += sent;
        remaining -= sent;
    }
    return true;
}

// Format an integer as a hex string (for chunked transfer encoding).
static std::string to_hex(size_t n) {
    if (n == 0) return "0";
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%zx", n);
    return buf;
}

// Read one CRLF-terminated line from the connection.
// Max `max_len` bytes (safety against runaway headers).
// Returns false on connection close/error (line may be partial).
static bool read_line(IConnection& conn, std::string& line,
                      size_t max_len = 8192) {
    line.clear();
    while (line.size() < max_len) {
        char c;
        int n = conn.read(&c, 1);
        if (n <= 0) return false;
        if (c == '\n') {
            // Strip trailing CR
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return true;
        }
        line += c;
    }
    return true; // truncated but not an error
}

} // namespace

// ---------------------------------------------------------------------------
// HttpServer
// ---------------------------------------------------------------------------

HttpServer::HttpServer(ISocketListener& listener, IInferenceEngine& engine)
    : listener_(listener), engine_(engine) {}

void HttpServer::serve() {
    listener_.run([this](std::unique_ptr<IConnection> conn) {
        handle_connection(*conn);
    });
}

// ---------------------------------------------------------------------------
// Connection handler
// ---------------------------------------------------------------------------

void HttpServer::handle_connection(IConnection& conn) {
    HttpRequest req = read_request_head(conn);

    if (!req.parse_ok) {
        send_error(conn, "Bad request");
        return;
    }

    // Read body
    std::string body;
    if (req.content_length > 0) {
        if (!read_body(conn, req.content_length, body)) {
            // Connection dropped while reading body — nothing to respond to.
            return;
        }
    }

    // Route
    if (req.method == "POST" && req.path == "/v1/chat/completions") {
        handle_chat(conn, body);
    } else if (req.method == "GET" && (req.path == "/" || req.path == "/health")) {
        // Health check / root probe.
        send_response(conn, 200, "OK", "text/plain", "OK");
    } else {
        send_404(conn);
    }
}

// ---------------------------------------------------------------------------
// Request parsing
// ---------------------------------------------------------------------------

HttpRequest HttpServer::read_request_head(IConnection& conn) {
    HttpRequest req;

    // Request line: METHOD SP path SP HTTP/1.x CRLF
    std::string line;
    if (!read_line(conn, line) || line.empty()) return req;

    std::istringstream rline(line);
    std::string version;
    rline >> req.method >> req.path >> version;
    if (req.method.empty() || req.path.empty()) return req;

    // Headers
    while (true) {
        if (!read_line(conn, line)) break;
        if (line.empty()) break; // blank line = end of headers

        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string name  = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        trim(name);
        trim(value);

        if (iequal(name, "content-length")) {
            try { req.content_length = std::stoi(value); }
            catch (...) { req.content_length = 0; }
        } else if (iequal(name, "content-type")) {
            req.content_type = value;
        }
    }

    req.parse_ok = true;
    return req;
}

bool HttpServer::read_body(IConnection& conn, int length, std::string& body) {
    body.resize(static_cast<size_t>(length));
    int total = 0;
    while (total < length) {
        int n = conn.read(&body[total],
                          static_cast<size_t>(length - total));
        if (n <= 0) {
            body.resize(static_cast<size_t>(total));
            return false;
        }
        total += n;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Chat handler
// ---------------------------------------------------------------------------

void HttpServer::handle_chat(IConnection& conn, const std::string& body) {
    // --- Parse JSON request body ---
    ChatRequest req;
    req.model       = json_parse_string(body, "model");
    req.messages    = json_parse_messages(body);
    req.max_tokens  = json_parse_int  (body, "max_tokens",  256);
    req.temperature = json_parse_float(body, "temperature", 1.0f);
    req.stream      = json_parse_bool (body, "stream",      false);

    if (req.messages.empty()) {
        send_error(conn, "messages array is required and must not be empty");
        return;
    }

    // Clamp max_tokens to a sane range.
    if (req.max_tokens <= 0) req.max_tokens = 256;

    static const std::string COMPLETION_ID = "chatcmpl-1";

    if (!req.stream) {
        // ---- Non-streaming: collect full response then send JSON ----
        ChatResponse resp = engine_.generate(req, [](const std::string&) {
            return true; // accept all tokens
        });

        const std::string finish_str =
            (resp.finish == FinishReason::Length) ? "length" : "stop";

        std::string json;
        json.reserve(256 + resp.content.size());
        json += "{\"id\":\"";
        json += COMPLETION_ID;
        json += "\",\"object\":\"chat.completion\""
                ",\"choices\":[{\"index\":0"
                ",\"message\":{\"role\":\"assistant\",\"content\":\"";
        json += json_escape(resp.content);
        json += "\"},\"finish_reason\":\"";
        json += finish_str;
        json += "\"}]"
                ",\"usage\":{\"prompt_tokens\":";
        char nbuf[32];
        std::snprintf(nbuf, sizeof(nbuf), "%d", resp.prompt_tokens);
        json += nbuf;
        json += ",\"completion_tokens\":";
        std::snprintf(nbuf, sizeof(nbuf), "%d", resp.completion_tokens);
        json += nbuf;
        json += "}}";

        send_response(conn, 200, "OK", "application/json", json);

    } else {
        // ---- Streaming: SSE with chunked transfer encoding ----

        // Write the SSE response headers first (no body yet).
        std::string header;
        header += "HTTP/1.1 200 OK\r\n";
        header += "Content-Type: text/event-stream\r\n";
        header += "Cache-Control: no-cache\r\n";
        header += "Transfer-Encoding: chunked\r\n";
        header += "Connection: close\r\n";
        header += "\r\n";
        if (!write_all(conn, header)) return;

        // Generate and stream each token.
        bool client_disconnected = false;

        engine_.generate(req, [&](const std::string& token) -> bool {
            if (client_disconnected) return false;

            // Build the SSE data line for this token.
            std::string data;
            data += "{\"id\":\"";
            data += COMPLETION_ID;
            data += "\",\"object\":\"chat.completion.chunk\""
                    ",\"choices\":[{\"index\":0"
                    ",\"delta\":{\"content\":\"";
            data += json_escape(token);
            data += "\"},\"finish_reason\":null}]}";

            if (!write_sse_chunk(conn, data)) {
                client_disconnected = true;
                return false;
            }
            return true;
        });

        if (!client_disconnected) {
            // Final [DONE] event
            write_sse_chunk(conn, "[DONE]");
            write_chunked_terminator(conn);
        }
    }
}

// ---------------------------------------------------------------------------
// Response helpers
// ---------------------------------------------------------------------------

void HttpServer::send_response(IConnection& conn,
                                int status_code,
                                const std::string& status_text,
                                const std::string& content_type,
                                const std::string& body) {
    std::string resp;
    resp.reserve(128 + body.size());

    char status_line[64];
    std::snprintf(status_line, sizeof(status_line),
                  "HTTP/1.1 %d %s\r\n", status_code, status_text.c_str());
    resp += status_line;

    char cl_hdr[32];
    std::snprintf(cl_hdr, sizeof(cl_hdr), "%zu", body.size());
    resp += "Content-Type: ";
    resp += content_type;
    resp += "\r\n";
    resp += "Content-Length: ";
    resp += cl_hdr;
    resp += "\r\n";
    resp += "Connection: close\r\n";
    resp += "\r\n";
    resp += body;

    write_all(conn, resp);
}

void HttpServer::send_error(IConnection& conn, const std::string& message) {
    std::string body;
    body += "{\"error\":{\"message\":\"";
    body += json_escape(message);
    body += "\",\"type\":\"invalid_request_error\"}}";
    send_response(conn, 400, "Bad Request", "application/json", body);
}

void HttpServer::send_404(IConnection& conn) {
    const std::string body =
        "{\"error\":{\"message\":\"Not found\","
        "\"type\":\"invalid_request_error\"}}";
    send_response(conn, 404, "Not Found", "application/json", body);
}

// SSE chunk format (chunked transfer encoding):
//   <hex-length>\r\n
//   data: <data_line>\n\n
//   \r\n
bool HttpServer::write_sse_chunk(IConnection& conn,
                                  const std::string& data_line) {
    // The SSE payload we'll send as one chunk
    std::string payload;
    payload += "data: ";
    payload += data_line;
    payload += "\n\n";

    // Chunked-TE wrapper
    std::string chunk;
    chunk += to_hex(payload.size());
    chunk += "\r\n";
    chunk += payload;
    chunk += "\r\n";

    return write_all(conn, chunk);
}

bool HttpServer::write_chunked_terminator(IConnection& conn) {
    return write_all(conn, "0\r\n\r\n");
}

} // namespace xbai
