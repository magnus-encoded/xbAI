// core/inference/OgaInferenceEngine.cpp
// ORT-GenAI (OGA) C-API implementation of IInferenceEngine.
// Plain C++20 only -- ZERO WinRT/GDK/Windows.h (ADR-0006 SS6).

#include "OgaInferenceEngine.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "model/GenaiConfig.h"

// ---------------------------------------------------------------------------
// OGA header: use the real header from the NuGet extract if available
// (XBAI_HAVE_OGA=ON), otherwise fall back to the local stub.
// ---------------------------------------------------------------------------
#ifdef XBAI_HAVE_OGA
#  include "ort_genai_c.h"
#else
#  include "oga_stub.h"
#endif

namespace xbai {

// ---------------------------------------------------------------------------
// RAII deleters
// ---------------------------------------------------------------------------

void OgaModelDeleter::operator()(OgaModel* p) const noexcept {
    if (p) OgaDestroyModel(p);
}

void OgaTokenizerDeleter::operator()(OgaTokenizer* p) const noexcept {
    if (p) OgaDestroyTokenizer(p);
}

// Thin RAII wrappers for objects that are local to generate().
struct SeqGuard {
    OgaSequences* ptr = nullptr;
    SeqGuard() = default;
    SeqGuard(const SeqGuard&) = delete;
    SeqGuard& operator=(const SeqGuard&) = delete;
    ~SeqGuard() { if (ptr) OgaDestroySequences(ptr); }
};

struct ParamsGuard {
    OgaGeneratorParams* ptr = nullptr;
    ParamsGuard() = default;
    ParamsGuard(const ParamsGuard&) = delete;
    ParamsGuard& operator=(const ParamsGuard&) = delete;
    ~ParamsGuard() { if (ptr) OgaDestroyGeneratorParams(ptr); }
};

struct GeneratorGuard {
    OgaGenerator* ptr = nullptr;
    GeneratorGuard() = default;
    GeneratorGuard(const GeneratorGuard&) = delete;
    GeneratorGuard& operator=(const GeneratorGuard&) = delete;
    ~GeneratorGuard() { if (ptr) OgaDestroyGenerator(ptr); }
};

struct TokStreamGuard {
    OgaTokenizerStream* ptr = nullptr;
    TokStreamGuard() = default;
    TokStreamGuard(const TokStreamGuard&) = delete;
    TokStreamGuard& operator=(const TokStreamGuard&) = delete;
    ~TokStreamGuard() { if (ptr) OgaDestroyTokenizerStream(ptr); }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Extract the error string from an OgaResult, destroy it, and return as
// std::string.  Takes ownership of `res`.
static std::string oga_take_error(OgaResult* res) {
    if (!res) return {};
    std::string msg = OgaResultGetError(res) ? OgaResultGetError(res) : "(unknown OGA error)";
    OgaDestroyResult(res);
    return msg;
}

// Macro: on OGA failure, set `error`, return false (only for load()).
#define OGA_CHECK_LOAD(expr, label)                                   \
    do {                                                              \
        OgaResult* _r = (expr);                                       \
        if (_r) { error = label + std::string(": ") + oga_take_error(_r); return false; } \
    } while (0)

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------

bool OgaInferenceEngine::load(const ModelConfig& model, std::string& error) {
    // Reset any previously loaded model.
    tokenizer_.reset();
    model_.reset();
    model_type_.clear();

    // Optionally read model_type from genai_config.json to drive prompt template.
    if (!model.config_path.empty()) {
        GenaiConfig gc;
        std::string cfg_err;
        if (parse_genai_config(model.config_path, gc, cfg_err)) {
            model_type_ = gc.model_type;
        }
        // Non-fatal if config parse fails -- we fall back to default template.
    }

    OgaModel* raw_model = nullptr;
    OGA_CHECK_LOAD(OgaCreateModel(model.root_path.c_str(), &raw_model), "OgaCreateModel");
    model_.reset(raw_model);

    OgaTokenizer* raw_tok = nullptr;
    OGA_CHECK_LOAD(OgaCreateTokenizer(model_.get(), &raw_tok), "OgaCreateTokenizer");
    tokenizer_.reset(raw_tok);

    return true;
}

// ---------------------------------------------------------------------------
// Prompt template
// ---------------------------------------------------------------------------

std::string OgaInferenceEngine::build_prompt(
    const std::vector<ChatMessage>& messages) const
{
    // Determine template family from model_type.
    // Default: Phi-3 / Phi-3.5 instruct template.
    // Add branches here when new model families are added.
    const bool is_phi3 =
        model_type_.empty() ||
        model_type_.find("phi") != std::string::npos ||
        model_type_.find("Phi") != std::string::npos;

    std::string prompt;

    if (is_phi3) {
        // Phi-3 chat template:
        //   <|system|>\n{content}<|end|>\n   (for system messages)
        //   <|user|>\n{content}<|end|>\n      (for user messages)
        //   <|assistant|>\n{content}<|end|>\n (for assistant turns)
        // Final assistant turn is opened without content to seed generation.
        for (const auto& msg : messages) {
            if (msg.role == "system") {
                prompt += "<|system|>\n" + msg.content + "<|end|>\n";
            } else if (msg.role == "user") {
                prompt += "<|user|>\n" + msg.content + "<|end|>\n";
            } else if (msg.role == "assistant") {
                prompt += "<|assistant|>\n" + msg.content + "<|end|>\n";
            }
        }
        prompt += "<|assistant|>\n";
    } else {
        // Generic fallback: role: content pairs separated by newlines.
        for (const auto& msg : messages) {
            prompt += msg.role + ": " + msg.content + "\n";
        }
        prompt += "assistant: ";
    }

    return prompt;
}

// ---------------------------------------------------------------------------
// generate
// ---------------------------------------------------------------------------

ChatResponse OgaInferenceEngine::generate(const ChatRequest& request,
                                           const TokenCallback& on_token) {
    ChatResponse resp;
    resp.model = request.model;

    if (!model_ || !tokenizer_) {
        resp.finish = FinishReason::Error;
        resp.error  = "Model not loaded; call load() before generate()";
        return resp;
    }

    // 1. Build and tokenize the prompt.
    const std::string prompt = build_prompt(request.messages);

    SeqGuard input_seqs;
    {
        OgaResult* r = OgaCreateSequences(&input_seqs.ptr);
        if (r) {
            resp.finish = FinishReason::Error;
            resp.error  = "OgaCreateSequences: " + oga_take_error(r);
            return resp;
        }
    }
    {
        OgaResult* r = OgaTokenizerEncode(tokenizer_.get(), prompt.c_str(), input_seqs.ptr);
        if (r) {
            resp.finish = FinishReason::Error;
            resp.error  = "OgaTokenizerEncode: " + oga_take_error(r);
            return resp;
        }
    }

    // Record prompt token count (sequence 0, length after encoding).
    resp.prompt_tokens = static_cast<int32_t>(
        OgaSequencesGetSequenceCount(input_seqs.ptr, 0));

    // 2. Create generator params.
    ParamsGuard params;
    {
        OgaResult* r = OgaCreateGeneratorParams(model_.get(), &params.ptr);
        if (r) {
            resp.finish = FinishReason::Error;
            resp.error  = "OgaCreateGeneratorParams: " + oga_take_error(r);
            return resp;
        }
    }

    // max_length = prompt tokens + completion cap.
    const double max_length =
        static_cast<double>(resp.prompt_tokens + request.max_tokens);
    {
        OgaResult* r = OgaGeneratorParamsSetSearchNumber(params.ptr, "max_length", max_length);
        if (r) {
            resp.finish = FinishReason::Error;
            resp.error  = "OgaGeneratorParamsSetSearchNumber(max_length): " + oga_take_error(r);
            return resp;
        }
    }
    // temperature: OGA search param "temperature".
    {
        OgaResult* r = OgaGeneratorParamsSetSearchNumber(
            params.ptr, "temperature", static_cast<double>(request.temperature));
        if (r) {
            // Non-fatal: temperature might not be supported by all models.
            OgaDestroyResult(r);
        }
    }

    // 3. Create generator.
    GeneratorGuard gen;
    {
        OgaResult* r = OgaCreateGenerator(model_.get(), params.ptr, &gen.ptr);
        if (r) {
            resp.finish = FinishReason::Error;
            resp.error  = "OgaCreateGenerator: " + oga_take_error(r);
            return resp;
        }
    }

    // Append input token sequences.
    {
        OgaResult* r = OgaGenerator_AppendTokenSequences(gen.ptr, input_seqs.ptr);
        if (r) {
            resp.finish = FinishReason::Error;
            resp.error  = "OgaGenerator_AppendTokenSequences: " + oga_take_error(r);
            return resp;
        }
    }

    // 4. Create a tokenizer stream for incremental (per-token) decoding.
    TokStreamGuard tok_stream;
    {
        OgaResult* r = OgaCreateTokenizerStream(tokenizer_.get(), &tok_stream.ptr);
        if (r) {
            resp.finish = FinishReason::Error;
            resp.error  = "OgaCreateTokenizerStream: " + oga_take_error(r);
            return resp;
        }
    }

    // 5. Decode loop.
    bool cancelled = false;
    int32_t completion_tokens = 0;

    while (!OgaGenerator_IsDone(gen.ptr)) {
        // Generate the next token.
        {
            OgaResult* r = OgaGenerator_GenerateNextToken(gen.ptr);
            if (r) {
                resp.finish = FinishReason::Error;
                resp.error  = "OgaGenerator_GenerateNextToken: " + oga_take_error(r);
                return resp;
            }
        }

        // Retrieve the latest token id (sequence 0, last element).
        const size_t seq_len = OgaGenerator_GetSequenceCount(gen.ptr, 0);
        const int32_t* seq_data = OgaGenerator_GetSequenceData(gen.ptr, 0);
        if (!seq_data || seq_len == 0) continue;

        const int32_t new_token = seq_data[seq_len - 1];
        ++completion_tokens;

        // Incrementally decode just this token via the stream.
        const char* piece = nullptr;
        {
            OgaResult* r = OgaTokenizerStreamDecode(tok_stream.ptr, new_token, &piece);
            if (r) {
                // Non-fatal: skip undecodable tokens.
                OgaDestroyResult(r);
                piece = nullptr;
            }
        }

        // If piece is non-empty, deliver it to the caller and accumulate.
        if (piece && piece[0] != '\0') {
            const std::string token_str(piece);
            // piece is owned by the stream (valid until next decode call) --
            // copy it into token_str before any further OGA calls.
            resp.content += token_str;
            if (on_token && !on_token(token_str)) {
                cancelled = true;
                break;
            }
        }
    }

    resp.completion_tokens = completion_tokens;
    resp.finish = cancelled         ? FinishReason::Stop   // client-driven stop
                : (completion_tokens >= request.max_tokens) ? FinishReason::Length
                : FinishReason::Stop;

    return resp;
}

}  // namespace xbai
