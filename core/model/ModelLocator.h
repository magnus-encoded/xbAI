// core/model/ModelLocator.h
// Resolves a model-id to its on-disk layout and validates genai_config.json.
// Plain C++ only -- ZERO WinRT/GDK/Windows.h (ADR-0006 SS6).
#pragma once

#include <string>
#include <vector>

#include "pal/types.h"

namespace xbai {

class ModelLocator {
public:
    // Resolve model_id under root_path.
    // On success: fills `out` (model_id, root_path, config_path) and returns true.
    // On failure: fills `error` with a human-readable message and returns false.
    // config_path is set only if genai_config.json exists and parses without error.
    static bool resolve(const std::string& root_path,
                        const std::string& model_id,
                        ModelConfig& out,
                        std::string& error);

    // Enumerate subdirectories of root_path and return them as model-ids.
    // Returns an empty vector if root_path does not exist or is not a directory.
    static std::vector<std::string> list_models(const std::string& root_path);
};

}  // namespace xbai
