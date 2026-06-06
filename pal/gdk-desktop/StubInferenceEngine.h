// pal/gdk-desktop/StubInferenceEngine.h
// No-op IInferenceEngine stub for the wave-1 shell wiring.  load() succeeds
// immediately; generate() returns a canned error response.  Replaced by
// OgaInferenceEngine once the model pipeline is wired.
#pragma once

#include "pal/IInferenceEngine.h"

namespace xbai {

class StubInferenceEngine : public IInferenceEngine {
public:
    StubInferenceEngine() = default;
    ~StubInferenceEngine() override = default;

    bool load(const ModelConfig& /*model*/, std::string& /*error*/) override {
        // Nothing to load -- stub always succeeds.
        return true;
    }

    ChatResponse generate(const ChatRequest& request,
                          const TokenCallback& /*on_token*/) override {
        ChatResponse resp;
        resp.model   = request.model.empty() ? "stub" : request.model;
        resp.content = "(stub engine -- no model loaded)";
        resp.finish  = FinishReason::Stop;
        resp.completion_tokens = 0;
        resp.prompt_tokens     = 0;
        return resp;
    }
};

}  // namespace xbai
