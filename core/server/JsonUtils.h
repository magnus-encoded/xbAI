// core/server/JsonUtils.h
// Minimal, header-only JSON helpers for hand-rolled HTTP/OpenAI response
// serialisation and request parsing.  Plain C++20 only -- ZERO WinRT/GDK/
// Windows.h (ADR-0006 SS6).
//
// These helpers cover only the shapes used by /v1/chat/completions.  They are
// NOT general-purpose JSON libraries.

#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "pal/types.h"

namespace xbai {

// ---------------------------------------------------------------------------
// Serialisation helpers
// ---------------------------------------------------------------------------

// Escape a raw string value for embedding inside a JSON string literal.
// Handles the characters that MUST be escaped per RFC 8259 SS7:
//   "  ->  \"
//   \  ->  \\
//   /  ->  \/   (optional but safe)
//   BS ->  \b
//   FF ->  \f
//   LF ->  \n
//   CR ->  \r
//   HT ->  \t
//   U+0000-U+001F -> \uXXXX
inline std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    // \uXXXX for other control chars
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Parsing helpers
// ---------------------------------------------------------------------------
// All parsers operate on a raw JSON string.  They are intentionally simple:
// they do NOT handle nested objects, arrays-of-arrays, or escaped unicode
// surrogates beyond what the spec requires for the OpenAI wire format.
// ---------------------------------------------------------------------------

namespace detail {

// Skip whitespace.
inline size_t skip_ws(const std::string& s, size_t pos) {
    while (pos < s.size() &&
           (s[pos] == ' ' || s[pos] == '\t' ||
            s[pos] == '\n' || s[pos] == '\r'))
        ++pos;
    return pos;
}

// Find the byte offset of the first occurrence of `key` as a JSON object key
// (i.e. "key": pattern, skipping embedded matches inside string values).
// Returns std::string::npos when not found.
inline size_t find_key(const std::string& json, const std::string& key) {
    // Build the search pattern: "key"
    const std::string pat = "\"" + key + "\"";

    size_t pos = 0;
    while (pos < json.size()) {
        size_t found = json.find(pat, pos);
        if (found == std::string::npos) return std::string::npos;

        // Skip past the key and any whitespace to the colon.
        size_t after_key = found + pat.size();
        after_key = skip_ws(json, after_key);
        if (after_key < json.size() && json[after_key] == ':') {
            return after_key + 1; // position just after the colon
        }
        pos = found + 1;
    }
    return std::string::npos;
}

// Parse a JSON string value starting at `pos` (which should point at the
// opening '"').  Returns the unescaped string and advances pos past the
// closing '"'.  Returns empty string on parse failure (pos unchanged on
// failure).
inline std::string parse_string_at(const std::string& json, size_t& pos) {
    size_t p = skip_ws(json, pos);
    if (p >= json.size() || json[p] != '"') return {};
    ++p; // skip opening quote

    std::string result;
    while (p < json.size()) {
        char c = json[p];
        if (c == '"') {
            pos = p + 1;
            return result;
        }
        if (c == '\\') {
            ++p;
            if (p >= json.size()) break;
            char esc = json[p];
            switch (esc) {
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                case '/':  result += '/';  break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                case 'u': {
                    // 4-hex-digit code point (BMP only; surrogates skipped)
                    if (p + 4 < json.size()) {
                        std::string hex = json.substr(p + 1, 4);
                        unsigned cp = 0;
                        for (char h : hex) {
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp += h - '0';
                            else if (h >= 'a' && h <= 'f') cp += 10 + h - 'a';
                            else if (h >= 'A' && h <= 'F') cp += 10 + h - 'A';
                        }
                        // Emit as UTF-8 (BMP only)
                        if (cp < 0x80) {
                            result += static_cast<char>(cp);
                        } else if (cp < 0x800) {
                            result += static_cast<char>(0xC0 | (cp >> 6));
                            result += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            result += static_cast<char>(0xE0 | (cp >> 12));
                            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        p += 4;
                    }
                    break;
                }
                default: result += esc; break;
            }
            ++p;
        } else {
            result += c;
            ++p;
        }
    }
    return {}; // unterminated string
}

} // namespace detail

// Extract a string value for a key from a flat (or top-level) JSON object.
// E.g. json_parse_string(body, "model") -> "phi-3.5-mini"
// Returns empty string when key not found or value is not a string.
inline std::string json_parse_string(const std::string& json,
                                      const std::string& key) {
    size_t val_start = detail::find_key(json, key);
    if (val_start == std::string::npos) return {};
    size_t pos = detail::skip_ws(json, val_start);
    return detail::parse_string_at(json, pos);
}

// Extract an integer value for a key.  Returns default_val when not found.
inline int json_parse_int(const std::string& json, const std::string& key,
                           int default_val) {
    size_t val_start = detail::find_key(json, key);
    if (val_start == std::string::npos) return default_val;
    size_t pos = detail::skip_ws(json, val_start);
    if (pos >= json.size()) return default_val;

    // Accept optional leading '-'
    bool neg = false;
    if (json[pos] == '-') { neg = true; ++pos; }
    if (pos >= json.size() || json[pos] < '0' || json[pos] > '9')
        return default_val;

    int val = 0;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        val = val * 10 + (json[pos] - '0');
        ++pos;
    }
    return neg ? -val : val;
}

// Extract a float value for a key.  Returns default_val when not found.
inline float json_parse_float(const std::string& json, const std::string& key,
                               float default_val) {
    size_t val_start = detail::find_key(json, key);
    if (val_start == std::string::npos) return default_val;
    size_t pos = detail::skip_ws(json, val_start);
    if (pos >= json.size()) return default_val;

    // Gather digits (integer + fractional + exponent) into a buffer and use
    // std::stof for accuracy.
    size_t start = pos;
    if (pos < json.size() && json[pos] == '-') ++pos;
    while (pos < json.size() &&
           (json[pos] >= '0' && json[pos] <= '9')) ++pos;
    if (pos < json.size() && json[pos] == '.') {
        ++pos;
        while (pos < json.size() &&
               (json[pos] >= '0' && json[pos] <= '9')) ++pos;
    }
    if (pos < json.size() && (json[pos] == 'e' || json[pos] == 'E')) {
        ++pos;
        if (pos < json.size() && (json[pos] == '+' || json[pos] == '-')) ++pos;
        while (pos < json.size() &&
               (json[pos] >= '0' && json[pos] <= '9')) ++pos;
    }
    if (start == pos) return default_val;
    try {
        return std::stof(json.substr(start, pos - start));
    } catch (...) {
        return default_val;
    }
}

// Extract a bool value (true/false literals) for a key.
inline bool json_parse_bool(const std::string& json, const std::string& key,
                             bool default_val) {
    size_t val_start = detail::find_key(json, key);
    if (val_start == std::string::npos) return default_val;
    size_t pos = detail::skip_ws(json, val_start);
    if (pos >= json.size()) return default_val;
    if (json.compare(pos, 4, "true")  == 0) return true;
    if (json.compare(pos, 5, "false") == 0) return false;
    return default_val;
}

// Parse the "messages" array from a /v1/chat/completions request body.
// Looks for "messages":[...] and parses each {role, content} object.
// Returns an empty vector on any parse failure.
inline std::vector<ChatMessage> json_parse_messages(const std::string& json) {
    std::vector<ChatMessage> result;

    // Find "messages":
    const std::string key = "\"messages\"";
    size_t k = json.find(key);
    if (k == std::string::npos) return result;

    size_t pos = detail::skip_ws(json, k + key.size());
    if (pos >= json.size() || json[pos] != ':') return result;
    pos = detail::skip_ws(json, pos + 1);
    if (pos >= json.size() || json[pos] != '[') return result;
    ++pos; // skip '['

    // Parse each object in the array.
    while (pos < json.size()) {
        pos = detail::skip_ws(json, pos);
        if (pos >= json.size()) break;
        if (json[pos] == ']') break;   // end of array
        if (json[pos] == ',') { ++pos; continue; }
        if (json[pos] != '{') break;   // unexpected

        // Find matching '}'
        // We look for "role" and "content" inside this object.
        size_t obj_start = pos;
        // Scan for the closing brace, tracking string-literal boundaries.
        int depth = 0;
        size_t obj_end = json.size();
        size_t scan = pos;
        while (scan < json.size()) {
            char c = json[scan];
            if (c == '{') { ++depth; ++scan; }
            else if (c == '}') { --depth; if (depth == 0) { obj_end = scan; ++scan; break; } ++scan; }
            else if (c == '"') {
                ++scan;
                // Skip string content
                while (scan < json.size()) {
                    if (json[scan] == '\\') { scan += 2; continue; }
                    if (json[scan] == '"') { ++scan; break; }
                    ++scan;
                }
            } else {
                ++scan;
            }
        }

        // Extract the object substring and parse role + content from it.
        std::string obj = json.substr(obj_start, obj_end - obj_start + 1);
        ChatMessage msg;
        msg.role    = json_parse_string(obj, "role");
        msg.content = json_parse_string(obj, "content");
        if (!msg.role.empty()) {
            result.push_back(std::move(msg));
        }

        pos = scan;
    }

    return result;
}

} // namespace xbai
