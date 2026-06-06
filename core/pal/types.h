// core/pal/types.h
// Core-internal value types shared across server / queue / inference / model.
// Plain C++ only. ZERO WinRT / GDK / platform headers (ADR-0006 §6).
//
// === FROZEN by P0 (2026-06-06) ===
// These signatures are the contract every parallel wave-1 task codes against.
// Do not change a field/signature after the P0 commit without cross-agent
// coordination (ADR-0006 §5). Additive, source-compatible growth (new optional
// fields) is the only change that does not need a re-freeze.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xbai {

// ---- OpenAI-compatible chat shapes (the wire contract, minus JSON) ----

// One message in a chat conversation. `role` is "system" | "user" | "assistant".
struct ChatMessage {
    std::string role;
    std::string content;
};

// A /v1/chat/completions request, already parsed off the wire.
struct ChatRequest {
    std::string model;                  // model-id the caller asked for
    std::vector<ChatMessage> messages;  // conversation so far
    int32_t max_tokens = 256;           // generation cap
    float temperature = 1.0f;
    bool stream = false;                // SSE if true, single JSON if false
};

// Why a generation stopped.
enum class FinishReason {
    Stop,    // model emitted end-of-sequence
    Length,  // hit max_tokens
    Error,   // generation failed (see ChatResponse::error)
};

// A completed (non-streamed) response, or the terminal frame of a stream.
struct ChatResponse {
    std::string model;        // model-id that served the request
    std::string content;      // full assistant text (empty for pure-stream)
    FinishReason finish = FinishReason::Stop;
    int32_t prompt_tokens = 0;
    int32_t completion_tokens = 0;
    std::string error;        // human-readable, set iff finish == Error
};

// ---- Model locator / config (filled by core/model) ----

// Resolved on-disk layout for one model-id. Paths are absolute, platform-native
// strings (the PAL hands these down; core treats them as opaque path strings).
struct ModelConfig {
    std::string model_id;     // logical id ("phi-3.5-mini", ...)
    std::string root_path;    // dir containing the model files
    std::string config_path;  // genai_config.json (OGA), if present
};

}  // namespace xbai
