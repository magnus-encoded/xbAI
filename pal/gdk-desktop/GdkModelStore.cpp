// pal/gdk-desktop/GdkModelStore.cpp
// Win32 IModelStore implementation. Win32 headers allowed here (pal layer).

#include "GdkModelStore.h"
#include "model/ModelLocator.h"

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace xbai {

namespace fs = std::filesystem;

GdkModelStore::GdkModelStore(std::string root_path)
    : root_path_(std::move(root_path)) {}

bool GdkModelStore::resolve(const std::string& model_id,
                             ModelConfig& out,
                             std::string& error) {
    return ModelLocator::resolve(root_path_, model_id, out, error);
}

std::vector<ModelFile> GdkModelStore::list_files(const std::string& model_id) {
    std::vector<ModelFile> result;

    // Derive the model directory without calling resolve() so a missing
    // genai_config.json does not suppress the listing.
    fs::path model_dir = fs::path(root_path_) / model_id;

    std::error_code ec;
    if (!fs::is_directory(model_dir, ec)) return result;

    for (const auto& entry :
         fs::recursive_directory_iterator(model_dir, ec)) {
        if (!entry.is_regular_file()) continue;

        ModelFile mf;
        // Relative path from the model root.
        mf.relative_path =
            fs::relative(entry.path(), model_dir, ec).string();
        mf.size_bytes = static_cast<int64_t>(entry.file_size(ec));
        result.push_back(std::move(mf));
    }

    return result;
}

// static
std::string GdkModelStore::default_root() {
#ifdef XBAI_HAVE_GDK
    // TODO: use GDK known-folder API (e.g. XPackageGetMountPath) once GDK is
    // available on this box. For now fall through to the Win32 path below.
#endif
    // Win32 fallback: place models/ next to the running executable.
    char exe_path[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        // Should never happen; return a relative sentinel that at least compiles.
        return "models";
    }

    fs::path exe_dir = fs::path(exe_path).parent_path();
    return (exe_dir / "models").string();
}

}  // namespace xbai
