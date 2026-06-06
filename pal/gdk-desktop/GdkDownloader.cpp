// pal/gdk-desktop/GdkDownloader.cpp
// WinHTTP-based IDownloader for the gdk-desktop backend.
//
// Resume logic:
//   1. HEAD the URL to read Content-Length.
//   2. Check existing file size on disk.
//      - If equal: file is complete, skip.
//      - If partial (0 < disk < remote): resume with Range: bytes=<disk>-
//      - If zero / no file: full GET.
//   3. Bytes are written to <dest>.tmp first; renamed to <dest> on success.
//
// Cancellation: cancel() sets a flag checked between files and after each
// ReadData chunk. on_done fires exactly once (ok=false on cancel/error).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "Winhttp.lib")

#include "GdkDownloader.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace xbai {

// --------------------------------------------------------------------------
// Internal helpers
// --------------------------------------------------------------------------

namespace {

// Narrow -> wide (UTF-8 assumed for URLs/paths — Windows file paths here are
// ASCII-safe for the model store; URLs from HuggingFace are ASCII).
static std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// Parse scheme, host, port, path out of an https:// URL.
// Returns false on malformed input.
struct ParsedUrl {
    std::wstring host;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    std::wstring path;   // includes query string
    bool secure = true;
};

static bool parse_url(const std::string& url, ParsedUrl& out) {
    std::wstring wurl = to_wide(url);

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);

    wchar_t scheme[16]{}, host[256]{}, path[2048]{};
    uc.lpszScheme    = scheme; uc.dwSchemeLength    = 16;
    uc.lpszHostName  = host;   uc.dwHostNameLength  = 256;
    uc.lpszUrlPath   = path;   uc.dwUrlPathLength   = 2048;

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return false;

    out.host   = host;
    out.port   = uc.nPort;
    out.path   = path;
    out.secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    return true;
}

// Query Content-Length via a HEAD request. Returns -1 on failure or if the
// server does not send Content-Length.
static int64_t head_content_length(HINTERNET session,
                                   const ParsedUrl& pu,
                                   const std::atomic<bool>& cancelled) {
    if (cancelled.load()) return -1;

    HINTERNET conn = WinHttpConnect(session, pu.host.c_str(), pu.port, 0);
    if (!conn) return -1;

    DWORD flags = pu.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"HEAD", pu.path.c_str(),
                                       nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    int64_t result = -1;
    if (req) {
        if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(req, nullptr)) {
            wchar_t buf[64]{};
            DWORD sz = sizeof(buf);
            if (WinHttpQueryHeaders(req,
                    WINHTTP_QUERY_CONTENT_LENGTH,
                    WINHTTP_HEADER_NAME_BY_INDEX,
                    buf, &sz, WINHTTP_NO_HEADER_INDEX)) {
                result = static_cast<int64_t>(_wtoi64(buf));
            }
        }
        WinHttpCloseHandle(req);
    }
    WinHttpCloseHandle(conn);
    return result;
}

// Download one file. Writes to dest_path + ".tmp", renames on completion.
// resume_from: number of bytes already on disk (0 = full download).
// Returns true on success. Fills error_out on failure.
static bool download_file(HINTERNET session,
                           const ParsedUrl& pu,
                           const std::filesystem::path& dest_path,
                           int64_t resume_from,
                           int64_t content_length,      // -1 if unknown
                           const std::string& rel_path,
                           int64_t global_bytes_done,   // bytes from prior files
                           int64_t global_bytes_total,  // 0 if unknown
                           int files_done,
                           int files_total,
                           const ProgressCallback& on_progress,
                           const std::atomic<bool>& cancelled,
                           std::string& error_out) {
    if (cancelled.load()) {
        error_out = "cancelled";
        return false;
    }

    std::filesystem::path tmp_path = dest_path;
    tmp_path += L".tmp";

    // If resuming, open in append mode; otherwise truncate.
    auto open_mode = std::ios::binary | (resume_from > 0 ? std::ios::app : std::ios::trunc);
    std::ofstream out(tmp_path, open_mode);
    if (!out) {
        error_out = "cannot open tmp file: " + tmp_path.string();
        return false;
    }

    HINTERNET conn = WinHttpConnect(session, pu.host.c_str(), pu.port, 0);
    if (!conn) {
        error_out = "WinHttpConnect failed: " + std::to_string(GetLastError());
        return false;
    }

    DWORD flags = pu.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", pu.path.c_str(),
                                       nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) {
        WinHttpCloseHandle(conn);
        error_out = "WinHttpOpenRequest failed: " + std::to_string(GetLastError());
        return false;
    }

    // Add Range header if resuming.
    bool ok = false;
    if (resume_from > 0) {
        std::wstring range = L"Range: bytes=" + std::to_wstring(resume_from) + L"-";
        ok = !!WinHttpSendRequest(req, range.c_str(), static_cast<DWORD>(range.size()),
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    } else {
        ok = !!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    }

    if (!ok || !WinHttpReceiveResponse(req, nullptr)) {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        error_out = "WinHttpSendRequest/ReceiveResponse failed: " + std::to_string(GetLastError());
        return false;
    }

    // Check HTTP status code.
    {
        DWORD status = 0;
        DWORD sz = sizeof(status);
        WinHttpQueryHeaders(req,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status, &sz, WINHTTP_NO_HEADER_INDEX);
        // 200 OK or 206 Partial Content are both acceptable.
        if (status != 200 && status != 206) {
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(conn);
            error_out = "HTTP " + std::to_string(status) + " for " + rel_path;
            return false;
        }
    }

    static const DWORD kChunkSize = 64 * 1024;  // 64 KiB
    std::vector<char> buf(kChunkSize);
    int64_t file_bytes = resume_from;

    for (;;) {
        if (cancelled.load()) {
            out.close();
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(conn);
            error_out = "cancelled";
            return false;
        }

        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) break;
        if (avail == 0) break;  // end of response

        DWORD to_read = (avail < kChunkSize) ? avail : kChunkSize;
        DWORD read = 0;
        if (!WinHttpReadData(req, buf.data(), to_read, &read)) break;
        if (read == 0) break;

        out.write(buf.data(), read);
        if (!out) {
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(conn);
            error_out = "write error for " + tmp_path.string();
            return false;
        }

        file_bytes += static_cast<int64_t>(read);

        if (on_progress) {
            DownloadProgress prog;
            prog.bytes_done   = global_bytes_done + file_bytes;
            prog.bytes_total  = global_bytes_total;
            prog.files_done   = files_done;
            prog.files_total  = files_total;
            prog.current_file = rel_path;
            on_progress(prog);
        }
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    out.close();

    // Validate size if we knew it up front.
    if (content_length > 0 && file_bytes != content_length) {
        error_out = "size mismatch for " + rel_path +
                    " (got " + std::to_string(file_bytes) +
                    ", expected " + std::to_string(content_length) + ")";
        return false;
    }

    // Atomic rename: remove existing dest if present, then rename tmp.
    std::error_code ec;
    if (std::filesystem::exists(dest_path, ec)) {
        std::filesystem::remove(dest_path, ec);
    }
    std::filesystem::rename(tmp_path, dest_path, ec);
    if (ec) {
        error_out = "rename failed for " + rel_path + ": " + ec.message();
        return false;
    }

    return true;
}

// The worker function executed on the background thread.
static void worker_fn(std::string dest_dir,
                      std::vector<DownloadItem> items,
                      ProgressCallback on_progress,
                      DownloadCallback on_done,
                      std::atomic<bool>* cancelled) {
    assert(on_done);

    HINTERNET session = WinHttpOpen(
        L"xbAI/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!session) {
        on_done(false, "WinHttpOpen failed: " + std::to_string(GetLastError()));
        return;
    }

    // Pre-pass: compute total bytes (best-effort via HEAD; 0 if unknown).
    // We do this once before the download loop so progress has a denominator.
    std::vector<int64_t> remote_sizes(items.size(), -1);
    int64_t total_bytes = 0;
    bool total_known = true;

    for (size_t i = 0; i < items.size(); ++i) {
        if (cancelled->load()) {
            WinHttpCloseHandle(session);
            on_done(false, "cancelled");
            return;
        }

        ParsedUrl pu;
        if (!parse_url(items[i].url, pu)) {
            // Skip pre-sizing for malformed URLs; they'll fail in the download pass.
            total_known = false;
            continue;
        }

        // Check if the file already exists with full size (we do the HEAD once
        // for both the pre-size pass and the skip check below).
        int64_t sz = head_content_length(session, pu, *cancelled);
        remote_sizes[i] = sz;

        std::filesystem::path dest =
            std::filesystem::path(dest_dir) / items[i].dest_relative_path;

        std::error_code ec;
        int64_t on_disk = 0;
        if (std::filesystem::exists(dest, ec)) {
            on_disk = static_cast<int64_t>(std::filesystem::file_size(dest, ec));
        }

        if (sz > 0 && on_disk == sz) {
            // Already complete — counts toward total but not as download work.
            total_bytes += sz;
        } else if (sz > 0) {
            total_bytes += sz;
        } else {
            total_known = false;
        }
    }

    if (!total_known) total_bytes = 0;

    // Download pass.
    int64_t global_bytes_done = 0;
    int files_done = 0;
    const int files_total = static_cast<int>(items.size());

    for (size_t i = 0; i < items.size(); ++i) {
        if (cancelled->load()) {
            WinHttpCloseHandle(session);
            on_done(false, "cancelled");
            return;
        }

        const DownloadItem& item = items[i];
        std::filesystem::path dest =
            std::filesystem::path(dest_dir) / item.dest_relative_path;

        // Ensure parent directory exists.
        std::error_code ec;
        std::filesystem::create_directories(dest.parent_path(), ec);

        int64_t remote_sz = remote_sizes[i];
        int64_t on_disk   = 0;
        if (std::filesystem::exists(dest, ec)) {
            on_disk = static_cast<int64_t>(std::filesystem::file_size(dest, ec));
        }

        // Skip if complete.
        if (remote_sz > 0 && on_disk == remote_sz) {
            global_bytes_done += remote_sz;
            ++files_done;

            if (on_progress) {
                DownloadProgress prog;
                prog.bytes_done   = global_bytes_done;
                prog.bytes_total  = total_bytes;
                prog.files_done   = files_done;
                prog.files_total  = files_total;
                prog.current_file = item.dest_relative_path;
                on_progress(prog);
            }
            continue;
        }

        // Partial resume: if file exists and is smaller than remote, resume.
        int64_t resume_from = (on_disk > 0 && (remote_sz < 0 || on_disk < remote_sz))
                              ? on_disk : 0;

        ParsedUrl pu;
        if (!parse_url(item.url, pu)) {
            WinHttpCloseHandle(session);
            on_done(false, "malformed URL: " + item.url);
            return;
        }

        std::string error;
        bool ok = download_file(
            session, pu, dest,
            resume_from, remote_sz,
            item.dest_relative_path,
            global_bytes_done, total_bytes,
            files_done, files_total,
            on_progress, *cancelled, error);

        if (!ok) {
            WinHttpCloseHandle(session);
            on_done(false, error);
            return;
        }

        global_bytes_done += remote_sz > 0 ? remote_sz : on_disk;
        ++files_done;
    }

    WinHttpCloseHandle(session);
    on_done(true, "");
}

}  // namespace

// --------------------------------------------------------------------------
// GdkDownloader
// --------------------------------------------------------------------------

GdkDownloader::~GdkDownloader() {
    // Signal and wait for the worker to finish. On_done will have already fired
    // (or will fire very soon) because cancel() sets the flag the worker checks.
    cancel();
    if (worker_.joinable()) worker_.join();
}

bool GdkDownloader::start(const std::string& dest_dir,
                           const std::vector<DownloadItem>& items,
                           const ProgressCallback& on_progress,
                           const DownloadCallback& on_done) {
    if (!on_done) return false;
    if (items.empty()) {
        on_done(true, "");
        return true;
    }

    // Join any previous worker before launching a new one.
    if (worker_.joinable()) worker_.join();

    cancelled_.store(false);

    try {
        worker_ = std::thread(worker_fn,
                              dest_dir, items,
                              on_progress, on_done,
                              &cancelled_);
    } catch (...) {
        on_done(false, "failed to launch worker thread");
        return false;
    }

    return true;
}

void GdkDownloader::cancel() {
    cancelled_.store(true);
}

}  // namespace xbai
