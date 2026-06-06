// pal/gdk-desktop/GdkDownloader.h
// IDownloader implementation for the gdk-desktop PAL backend.
// Uses WinHTTP for HTTPS transfers with resume support (Range: bytes=N-).
// Downloads run on a worker thread; start() returns immediately. on_done fires
// exactly once: on success, cancellation, or the first fatal transfer error.
#pragma once

#include "pal/IDownloader.h"

#include <atomic>
#include <thread>

namespace xbai {

class GdkDownloader : public IDownloader {
public:
    GdkDownloader() = default;
    ~GdkDownloader() override;

    // Starts the download asynchronously. Returns false only if a background
    // thread cannot be launched (extremely unlikely). on_done fires on the
    // worker thread — caller must marshal to their preferred thread if needed.
    bool start(const std::string& dest_dir,
               const std::vector<DownloadItem>& items,
               const ProgressCallback& on_progress,
               const DownloadCallback& on_done) override;

    // Signals the worker to stop after the current chunk. on_done fires with
    // ok=false. Safe to call before start() or after on_done has fired (no-op).
    void cancel() override;

private:
    std::thread        worker_;
    std::atomic<bool>  cancelled_{false};

    // Disallow copy/move — owns a thread handle.
    GdkDownloader(const GdkDownloader&) = delete;
    GdkDownloader& operator=(const GdkDownloader&) = delete;
};

}  // namespace xbai
