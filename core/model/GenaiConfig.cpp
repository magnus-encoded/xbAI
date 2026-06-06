// core/model/GenaiConfig.cpp
// Minimal hand-rolled parser for genai_config.json.
// We only need three fields; avoid a full JSON library dependency.
// Plain C++ only -- ZERO WinRT/GDK/Windows.h (ADR-0006 SS6).

#include "GenaiConfig.h"

#include <charconv>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace xbai {

namespace {

// Trim leading/trailing ASCII whitespace from a string_view.
std::string_view trim(std::string_view s) {
    const char* ws = " \t\r\n";
    auto start = s.find_first_not_of(ws);
    if (start == std::string_view::npos) return {};
    auto end = s.find_last_not_of(ws);
    return s.substr(start, end - start + 1);
}

// Find the value for a given JSON string key in `json`.
// Handles the simple flat-object form used by genai_config.json.
// Returns the raw value substring (quoted for strings, bare for numbers/bools),
// or empty if not found.
std::string_view find_json_value(std::string_view json, std::string_view key) {
    // Build the search pattern: "key"
    std::string pattern;
    pattern.reserve(key.size() + 2);
    pattern += '"';
    pattern += key;
    pattern += '"';

    auto pos = json.find(pattern);
    while (pos != std::string_view::npos) {
        auto after_key = pos + pattern.size();
        // Skip whitespace then expect ':'
        while (after_key < json.size() &&
               (json[after_key] == ' ' || json[after_key] == '\t' ||
                json[after_key] == '\r' || json[after_key] == '\n')) {
            ++after_key;
        }
        if (after_key >= json.size() || json[after_key] != ':') {
            // Not a key-value colon; search again past this match
            pos = json.find(pattern, pos + 1);
            continue;
        }
        ++after_key;  // skip ':'

        // Skip whitespace
        while (after_key < json.size() &&
               (json[after_key] == ' ' || json[after_key] == '\t' ||
                json[after_key] == '\r' || json[after_key] == '\n')) {
            ++after_key;
        }
        if (after_key >= json.size()) return {};

        // Determine the extent of the value
        char first = json[after_key];
        if (first == '"') {
            // Quoted string: scan for the closing quote (skip escaped quotes)
            auto start = after_key;
            auto i = after_key + 1;
            while (i < json.size()) {
                if (json[i] == '\\') { i += 2; continue; }
                if (json[i] == '"') { ++i; break; }
                ++i;
            }
            return json.substr(start, i - start);
        } else {
            // Number / bool / null: scan until delimiter
            auto start = after_key;
            auto i = after_key;
            while (i < json.size() && json[i] != ',' && json[i] != '}' &&
                   json[i] != ']' && json[i] != '\r' && json[i] != '\n') {
                ++i;
            }
            return trim(json.substr(start, i - start));
        }
    }
    return {};
}

// Unquote a JSON string value (remove surrounding quotes, no full unescape needed).
std::string unquote(std::string_view v) {
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
        return std::string(v.substr(1, v.size() - 2));
    }
    return std::string(v);
}

// Parse an integer from a string_view; returns false on failure.
bool parse_int(std::string_view v, int32_t& out) {
    auto trimmed = trim(v);
    auto result = std::from_chars(trimmed.data(),
                                  trimmed.data() + trimmed.size(), out);
    return result.ec == std::errc{};
}

}  // namespace

bool parse_genai_config(const std::string& file_path,
                        GenaiConfig& out,
                        std::string& error) {
    std::ifstream f(file_path);
    if (!f.is_open()) {
        error = "cannot open: " + file_path;
        return false;
    }

    std::ostringstream ss;
    ss << f.rdbuf();
    if (f.bad()) {
        error = "read error: " + file_path;
        return false;
    }
    std::string json = ss.str();
    std::string_view jv(json);

    out = {};

    // model_type
    auto v = find_json_value(jv, "model_type");
    if (!v.empty()) {
        out.model_type = unquote(v);
    }

    // num_key_value_heads
    v = find_json_value(jv, "num_key_value_heads");
    if (!v.empty()) {
        int32_t n = 0;
        if (parse_int(v, n)) out.num_key_value_heads = n;
    }

    // context_length
    v = find_json_value(jv, "context_length");
    if (!v.empty()) {
        int32_t n = 0;
        if (parse_int(v, n)) out.context_length = n;
    }

    return true;
}

}  // namespace xbai
