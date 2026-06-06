// pal/gdk-desktop/GdkModelStore.h
// Win32 path-based IModelStore. Resolves model roots from the filesystem via
// ModelLocator (core logic), with file enumeration via std::filesystem.
// Win32 headers (GetModuleFileNameA) are allowed in pal/ -- see ADR-0004.
#pragma once

#include "pal/IModelStore.h"

#include <string>
#include <vector>

namespace xbai {

class GdkModelStore : public IModelStore {
public:
    // Construct with an explicit model root (absolute Win32 path).
    explicit GdkModelStore(std::string root_path);

    // IModelStore
    bool resolve(const std::string& model_id,
                 ModelConfig& out,
                 std::string& error) override;

    std::vector<ModelFile> list_files(const std::string& model_id) override;

    // Default root for dev-PC use: <exe-dir>\models.
    // When XBAI_HAVE_GDK is defined, this would delegate to a GDK known-folder
    // API instead; gated until GDK is present.
    static std::string default_root();

private:
    std::string root_path_;
};

}  // namespace xbai
