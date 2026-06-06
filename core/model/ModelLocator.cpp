// core/model/ModelLocator.cpp
// Plain C++ only -- ZERO WinRT/GDK/Windows.h (ADR-0006 SS6).

#include "ModelLocator.h"
#include "GenaiConfig.h"
#include "pal/types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xbai {

namespace fs = std::filesystem;

bool ModelLocator::resolve(const std::string& root_path,
                           const std::string& model_id,
                           ModelConfig& out,
                           std::string& error) {
    if (model_id.empty()) {
        error = "model_id must not be empty";
        return false;
    }

    fs::path model_dir = fs::path(root_path) / model_id;

    std::error_code ec;
    if (!fs::is_directory(model_dir, ec)) {
        error = "model directory not found: " + model_dir.string();
        return false;
    }

    out.model_id   = model_id;
    out.root_path  = model_dir.string();
    out.config_path = "";

    fs::path config_file = model_dir / "genai_config.json";
    if (fs::is_regular_file(config_file, ec)) {
        // Validate it parses; we don't need the struct here, just confirmation.
        GenaiConfig cfg;
        std::string parse_err;
        if (!parse_genai_config(config_file.string(), cfg, parse_err)) {
            error = "genai_config.json parse error: " + parse_err;
            return false;
        }
        out.config_path = config_file.string();
    }
    // If genai_config.json is absent, config_path stays empty -- not an error;
    // some model layouts (e.g. raw ONNX without OGA wrapper) lack it.

    return true;
}

std::vector<std::string> ModelLocator::list_models(const std::string& root_path) {
    std::vector<std::string> ids;
    std::error_code ec;

    fs::path root(root_path);
    if (!fs::is_directory(root, ec)) return ids;

    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory()) continue;
        ids.push_back(entry.path().filename().string());
    }

    return ids;
}

}  // namespace xbai
