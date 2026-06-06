// core/pal/IDownloader.h
// PAL seam: fetch a file set into a directory, resumable, with progress.
// The platform owns the transfer mechanism (BackgroundDownloader on Xbox,
// WinHTTP/XCurl on desktop). Plain C++ only.
//
// === FROZEN by P0 (2026-06-06) === see types.h for the freeze rule.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace xbai {

// One file to fetch: a source URL and its destination path under the dest dir.
struct DownloadItem {
    std::string url;
    std::string dest_relative_path;  // relative to the download dir
};

// Progress for an in-flight file set. Invoked periodically on a platform thread.
struct DownloadProgress {
    int64_t bytes_done = 0;
    int64_t bytes_total = 0;   // 0 if unknown
    int files_done = 0;
    int files_total = 0;
    std::string current_file;  // dest_relative_path being fetched
};

// Reports a file-set download's terminal outcome (true == all items complete).
using DownloadCallback = std::function<void(bool ok, const std::string& error)>;
using ProgressCallback = std::function<void(const DownloadProgress&)>;

// Fetches a set of files into a directory; resumes partial files where possible.
class IDownloader {
public:
    virtual ~IDownloader() = default;

    // Start fetching `items` into `dest_dir`. Resumable across restarts. Calls
    // `on_progress` periodically and `on_done` exactly once at the end. Returns
    // false immediately if the request could not be started (sets nothing —
    // failure to *start* is reported via the return; failures *during* the
    // transfer arrive through on_done).
    virtual bool start(const std::string& dest_dir,
                      const std::vector<DownloadItem>& items,
                      const ProgressCallback& on_progress,
                      const DownloadCallback& on_done) = 0;

    // Request cancellation of the in-flight set. on_done still fires (ok=false).
    virtual void cancel() = 0;
};

}  // namespace xbai
