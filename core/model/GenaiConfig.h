// core/model/GenaiConfig.h
// Minimal parser for genai_config.json (ORT-GenAI model config).
// Plain C++ only -- ZERO WinRT/GDK/Windows.h (ADR-0006 SS6).
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace xbai {

// Fields from genai_config.json that core/inference needs.
// All fields are optional -- we treat any subset as valid.
struct GenaiConfig {
    std::string model_type;                   // e.g. "phi3", "llama", "mistral"
    std::optional<int32_t> num_key_value_heads;
    std::optional<int32_t> context_length;
};

// Parse genai_config.json from the given file path.
// Returns true and fills `out` on success.
// Returns false and fills `error` on failure (file unreadable, JSON malformed).
bool parse_genai_config(const std::string& file_path,
                        GenaiConfig& out,
                        std::string& error);

}  // namespace xbai
