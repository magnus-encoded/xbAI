// core/inference/oga_stub.h
// Stub declarations for the ORT-GenAI C API.
// Used when XBAI_HAVE_OGA is OFF (dev PC without the real OGA DLL/lib).
// Provides the same type names and function declarations as ort_genai_c.h so
// that OgaInferenceEngine.cpp compiles without the real SDK installed.
//
// These bodies are intentionally NOT defined here -- the translation unit will
// link against the real onnxruntime-genai.lib when XBAI_HAVE_OGA is turned ON
// in a backend that has the DLL present.  With XBAI_HAVE_OGA=OFF the .cpp
// compiles but the inference target is not linked into any executable that
// actually calls OgaCreateModel, so there is no unresolved-symbol error.

#pragma once

#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Opaque handle types
// ---------------------------------------------------------------------------
typedef struct OgaResult          OgaResult;
typedef struct OgaModel           OgaModel;
typedef struct OgaTokenizer       OgaTokenizer;
typedef struct OgaTokenizerStream OgaTokenizerStream;
typedef struct OgaSequences       OgaSequences;
typedef struct OgaGeneratorParams OgaGeneratorParams;
typedef struct OgaGenerator       OgaGenerator;

// ---------------------------------------------------------------------------
// Function declarations (no bodies -- link-time resolution only)
// ---------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif

// Result helpers
const char* OgaResultGetError(const OgaResult* result);
void        OgaDestroyResult(OgaResult* result);
void        OgaDestroyString(const char* str);

// Model
OgaResult* OgaCreateModel(const char* config_path, OgaModel** out);
void       OgaDestroyModel(OgaModel* model);

// Tokenizer
OgaResult* OgaCreateTokenizer(const OgaModel* model, OgaTokenizer** out);
void       OgaDestroyTokenizer(OgaTokenizer* tokenizer);
OgaResult* OgaTokenizerEncode(const OgaTokenizer* tokenizer, const char* str,
                               OgaSequences* sequences);
OgaResult* OgaTokenizerDecode(const OgaTokenizer* tokenizer,
                               const int32_t* tokens, size_t token_count,
                               const char** out_string);

// Tokenizer stream (incremental per-token decode)
OgaResult* OgaCreateTokenizerStream(const OgaTokenizer* tokenizer,
                                     OgaTokenizerStream** out);
void       OgaDestroyTokenizerStream(OgaTokenizerStream* stream);
OgaResult* OgaTokenizerStreamDecode(OgaTokenizerStream* stream, int32_t token,
                                     const char** out);

// Sequences
OgaResult*      OgaCreateSequences(OgaSequences** out);
void            OgaDestroySequences(OgaSequences* sequences);
size_t          OgaSequencesGetSequenceCount(const OgaSequences* sequences,
                                              size_t sequence_index);
const int32_t*  OgaSequencesGetSequenceData(const OgaSequences* sequences,
                                             size_t sequence_index);

// Generator params
OgaResult* OgaCreateGeneratorParams(const OgaModel* model,
                                     OgaGeneratorParams** out);
void       OgaDestroyGeneratorParams(OgaGeneratorParams* params);
OgaResult* OgaGeneratorParamsSetSearchNumber(OgaGeneratorParams* params,
                                              const char* name, double value);

// Generator
OgaResult*     OgaCreateGenerator(const OgaModel* model,
                                   const OgaGeneratorParams* params,
                                   OgaGenerator** out);
void           OgaDestroyGenerator(OgaGenerator* generator);
bool           OgaGenerator_IsDone(const OgaGenerator* generator);
OgaResult*     OgaGenerator_AppendTokenSequences(OgaGenerator* generator,
                                                  const OgaSequences* sequences);
OgaResult*     OgaGenerator_GenerateNextToken(OgaGenerator* generator);
size_t         OgaGenerator_GetSequenceCount(const OgaGenerator* generator,
                                              size_t index);
const int32_t* OgaGenerator_GetSequenceData(const OgaGenerator* generator,
                                             size_t index);

#ifdef __cplusplus
}
#endif
