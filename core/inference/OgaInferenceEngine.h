// core/inference/OgaInferenceEngine.h
// ORT-GenAI (OGA) C-API implementation of IInferenceEngine.
// Plain C++20 only -- ZERO WinRT/GDK/Windows.h (ADR-0006 SS6).
#pragma once

#include <memory>
#include <string>

#include "pal/IInferenceEngine.h"
#include "pal/types.h"

// Forward-declare OGA opaque types so this header stays clean of ort_genai_c.h.
struct OgaModel;
struct OgaTokenizer;

namespace xbai {

// OGA RAII helpers (defined in the .cpp; forward-declared here so the class
// can hold unique_ptrs to them without exposing OGA headers to consumers).
struct OgaModelDeleter   { void operator()(OgaModel*)     const noexcept; };
struct OgaTokenizerDeleter { void operator()(OgaTokenizer*) const noexcept; };

class OgaInferenceEngine final : public IInferenceEngine {
public:
    OgaInferenceEngine() = default;
    ~OgaInferenceEngine() override = default;

    // Not copyable or moveable -- OGA state is not relocatable.
    OgaInferenceEngine(const OgaInferenceEngine&) = delete;
    OgaInferenceEngine& operator=(const OgaInferenceEngine&) = delete;

    // IInferenceEngine
    bool load(const ModelConfig& model, std::string& error) override;
    ChatResponse generate(const ChatRequest& request,
                          const TokenCallback& on_token) override;

private:
    // Build the prompt string for a given model_type.
    // Defaults to Phi-3 chat template; add other templates as needed.
    std::string build_prompt(const std::vector<ChatMessage>& messages) const;

    std::unique_ptr<OgaModel,     OgaModelDeleter>     model_;
    std::unique_ptr<OgaTokenizer, OgaTokenizerDeleter> tokenizer_;
    std::string model_type_;  // from genai_config, drives prompt template
};

}  // namespace xbai
