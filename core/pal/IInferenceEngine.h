// core/pal/IInferenceEngine.h
// The contract the server/queue call to run a decode. NOT a platform seam
// (the OGA wrapper in core/inference implements it); it lives here so it freezes
// alongside the wire types it speaks. Plain C++ only.
//
// === FROZEN by P0 (2026-06-06) === see types.h header for the freeze rule.

#pragma once

#include <functional>
#include <string>

#include "types.h"

namespace xbai {

// Streaming token sink. Called once per decoded token (or token-chunk) on the
// decode worker thread. Return false to ask the engine to stop generating
// (client disconnect / cancellation).
using TokenCallback = std::function<bool(const std::string& token)>;

// Runs a single generation at a time (single-flight is enforced by core/queue,
// not here). Implementations need not be thread-safe; core owns serialization.
class IInferenceEngine {
public:
    virtual ~IInferenceEngine() = default;

    // Load/prepare a model. Returns false and sets `error` on failure.
    virtual bool load(const ModelConfig& model, std::string& error) = 0;

    // Run one generation. Tokens are delivered via `on_token` as they decode;
    // the returned ChatResponse carries the terminal accounting (finish reason,
    // token counts, full content, or an error). Blocking call.
    virtual ChatResponse generate(const ChatRequest& request,
                                  const TokenCallback& on_token) = 0;
};

}  // namespace xbai
