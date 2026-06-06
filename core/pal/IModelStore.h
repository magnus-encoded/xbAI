// core/pal/IModelStore.h
// PAL seam: resolve where a model-id's files live on this platform, and
// enumerate/stat them. The platform knows the storage layout (LocalState on
// Xbox, a Win32 dir on desktop); core treats the returned paths as opaque.
// Plain C++ only.
//
// === FROZEN by P0 (2026-06-06) === see types.h for the freeze rule.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "types.h"

namespace xbai {

// One file under a model root.
struct ModelFile {
    std::string relative_path;  // path relative to the model root
    int64_t size_bytes = 0;
};

// Maps a logical model-id to its on-disk location for this platform.
class IModelStore {
public:
    virtual ~IModelStore() = default;

    // Resolve a model-id to its root path + config. Returns false / sets `error`
    // if the model is not present locally.
    virtual bool resolve(const std::string& model_id,
                         ModelConfig& out,
                         std::string& error) = 0;

    // List the files under a resolved model root. Empty if the root is missing.
    virtual std::vector<ModelFile> list_files(const std::string& model_id) = 0;
};

}  // namespace xbai
