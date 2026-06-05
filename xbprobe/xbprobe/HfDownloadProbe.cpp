#include "pch.h"
#include "Probe.h"

#include <atomic>
#include <iomanip>
#include <memory>
#include <mutex>
#include <string>
#include <ppltasks.h>

// ---------------------------------------------------------------------------
// T4 gate: prove the v1 model-acquisition path (CONTEXT #3) on the real console —
// fetch the Phi-3-mini INT4 *file set* from Hugging Face, on-device, into
// LocalState\models\<id>\ using the UWP-sanctioned BackgroundDownloader (resumable,
// survives app suspend/terminate). Verifies each file lands at its exact expected
// size, that the layout the loader needs (model.onnx + external model.onnx.data
// together) is reproduced, and — across two launches — that an interrupted transfer
// RESUMES rather than restarts (GetCurrentDownloadsAsync re-attach).
//
// Runs on a ThreadPool worker, NOT the app main thread: the 2 GB file takes minutes
// and blocking the dispatcher that long trips the UWP activation watchdog. The
// worker live-rewrites the LocalState report so progress is pullable via WDP.
// ---------------------------------------------------------------------------

using namespace Windows::Foundation;
using namespace Windows::Storage;
using namespace Windows::Networking::BackgroundTransfer;
using namespace Windows::System::Threading;
using namespace concurrency;

namespace
{
    std::mutex        g_mtx;
    std::wstring      g_prefix;                 // report up to the T4 section (capability output)
    std::wstring      g_body;                   // permanent T4 lines (per-file start/done + summary)
    std::wstring      g_live;                   // transient in-flight progress line (overwritten)
    std::atomic<bool> g_complete{ false };      // all 9 files present at expected size
    IAsyncAction^     g_work = nullptr;         // keep the worker alive

    void Flush() // caller holds g_mtx
    {
        WriteReport(g_prefix + g_body + g_live);
    }

    void AppendBody(const std::wstring& line)
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_body += line;
        g_live.clear();
        Flush();
    }

    std::wstring MB(unsigned long long b)
    {
        std::wstringstream s;
        s << std::fixed << std::setprecision(1) << (double)b / (1024.0 * 1024.0) << L" MB";
        return s.str();
    }

    // Bytes-on-disk for a file in `folder`, or -1 if absent/error.
    long long DiskSize(StorageFolder^ folder, Platform::String^ name)
    {
        try
        {
            auto item = create_task(folder->TryGetItemAsync(name)).get();
            if (item == nullptr || !item->IsOfType(StorageItemTypes::File)) return -1;
            StorageFile^ f = safe_cast<StorageFile^>(item);
            auto props = create_task(f->GetBasicPropertiesAsync()).get();
            return (long long)props->Size;
        }
        catch (...) { return -1; }
    }

    // Drive a DownloadOperation async (Start/Attach result) to completion WITHOUT
    // pumping a dispatcher (we're on a threadpool thread): a completion event + a
    // throttled progress handler that live-writes the report. Returns AsyncStatus;
    // `errOut` carries the HRESULT on failure.
    AsyncStatus AwaitDownload(
        IAsyncOperationWithProgress<DownloadOperation^, DownloadOperation^>^ async,
        const std::wstring& tag, unsigned long long expected, HRESULT& errOut)
    {
        HANDLE done = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        AsyncStatus fin = AsyncStatus::Error;
        errOut = S_OK;
        auto lastLogged = std::make_shared<unsigned long long>(0ULL);
        ULONGLONG startTick = GetTickCount64();

        async->Progress = ref new AsyncOperationProgressHandler<DownloadOperation^, DownloadOperation^>(
            [tag, expected, lastLogged, startTick](IAsyncOperationWithProgress<DownloadOperation^, DownloadOperation^>^, DownloadOperation^ dop)
            {
                auto p = dop->Progress;
                unsigned long long rec = p.BytesReceived;
                unsigned long long tot = p.TotalBytesToReceive ? p.TotalBytesToReceive : expected;
                // Throttle: log every 64 MB (and always the final tick).
                if (rec >= *lastLogged && rec - *lastLogged < (64ULL << 20) && rec != tot) return;
                *lastLogged = rec;
                double secs = (GetTickCount64() - startTick) / 1000.0;
                double mbps = secs > 0 ? (rec / (1024.0 * 1024.0)) / secs : 0.0;
                std::wstringstream s;
                s << L"    [..] " << tag << L"  " << MB(rec) << L" / " << MB(tot);
                if (tot) s << L"  (" << std::fixed << std::setprecision(0) << (100.0 * rec / tot) << L"%)";
                s << L"  " << std::fixed << std::setprecision(1) << mbps << L" MB/s\n";
                std::lock_guard<std::mutex> lk(g_mtx);
                g_live = s.str();
                Flush();
            });

        async->Completed = ref new AsyncOperationWithProgressCompletedHandler<DownloadOperation^, DownloadOperation^>(
            [&fin, &errOut, done](IAsyncOperationWithProgress<DownloadOperation^, DownloadOperation^>^ a, AsyncStatus s)
            {
                fin = s;
                if (s != AsyncStatus::Completed) { try { errOut = a->ErrorCode.Value; } catch (...) {} }
                SetEvent(done);
            });

        WaitForSingleObject(done, INFINITE);
        CloseHandle(done);
        return fin;
    }

    struct FileSpec { const wchar_t* name; unsigned long long size; };

    // Smalls first, the 2 GB external-data blob LAST — so an interrupt window lands
    // on the big file and the resume test exercises the meaningful case.
    const FileSpec kFiles[] = {
        { L"added_tokens.json",       306ULL },
        { L"config.json",             919ULL },
        { L"genai_config.json",       1622ULL },
        { L"special_tokens_map.json", 599ULL },
        { L"tokenizer_config.json",   3441ULL },
        { L"tokenizer.model",         499723ULL },
        { L"tokenizer.json",          1937869ULL },
        { L"model.onnx",              2109332ULL },
        { L"model.onnx.data",         2131292928ULL },   // ONNX external weights; just under the 2 GiB UWP cap
    };
    const wchar_t* kBaseUrl =
        L"https://huggingface.co/microsoft/Phi-3-mini-4k-instruct-onnx/resolve/main/directml/directml-int4-awq-block-128/";

    void DownloadWorker()
    {
        unsigned int dummy = 0; (void)dummy;
        std::wstringstream r;
        r << L"\n================ T4: on-device Hugging Face download ================\n";
        r << L"[T4] repo : microsoft/Phi-3-mini-4k-instruct-onnx  (directml-int4-awq-block-128)\n";
        r << L"[T4] dest : LocalState\\models\\phi3-mini-int4-dl\\   via BackgroundDownloader\n";
        r << L"[T4] files: 9 (8 small + model.onnx.data = 2,131,292,928 B)\n";
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            g_body = r.str();
            Flush();
        }

        try
        {
            StorageFolder^ local  = ApplicationData::Current->LocalFolder;
            StorageFolder^ models = create_task(local->CreateFolderAsync(L"models", CreationCollisionOption::OpenIfExists)).get();
            StorageFolder^ dl     = create_task(models->CreateFolderAsync(L"phi3-mini-int4-dl", CreationCollisionOption::OpenIfExists)).get();

            BackgroundDownloader^ downloader = ref new BackgroundDownloader();
            auto current = create_task(BackgroundDownloader::GetCurrentDownloadsAsync()).get();
            {
                std::wstringstream s;
                s << L"[T4] GetCurrentDownloadsAsync: " << current->Size << L" pre-existing transfer(s)";
                if (current->Size > 0) s << L"  -> RESUME path (a prior launch was interrupted)";
                else                   s << L"  -> fresh start";
                s << L"\n";
                AppendBody(s.str());
            }

            for (auto& f : kFiles)
            {
                Platform::String^ name = ref new Platform::String(f.name);
                std::wstring tag(f.name);

                // 1) already in-flight from a prior session? -> attach (resume).
                DownloadOperation^ inflight = nullptr;
                for (unsigned i = 0; i < current->Size; ++i)
                {
                    auto o = current->GetAt(i);
                    if (o->ResultFile != nullptr && o->ResultFile->Name == name) { inflight = o; break; }
                }

                if (inflight != nullptr)
                {
                    unsigned long long already = inflight->Progress.BytesReceived;
                    std::wstringstream s;
                    s << L"  [RESUME] " << tag << L"  (" << MB(already) << L" already on disk; continuing, not restarting)\n";
                    AppendBody(s.str());

                    HRESULT err = S_OK;
                    ULONGLONG t0 = GetTickCount64();
                    AsyncStatus st = AwaitDownload(inflight->AttachAsync(), tag, f.size, err);
                    double secs = (GetTickCount64() - t0) / 1000.0;
                    long long got = DiskSize(dl, name);
                    std::wstringstream d;
                    d << L"  [DONE]   " << tag << L"  " << got << L"/" << f.size << L" B  "
                      << (st == AsyncStatus::Completed && got == (long long)f.size ? L"OK (resumed)" : L"FAIL")
                      << L"  " << std::fixed << std::setprecision(1) << secs << L"s";
                    if (st != AsyncStatus::Completed) d << L"  hr=0x" << std::hex << (uint32_t)err;
                    d << L"\n";
                    AppendBody(d.str());
                    continue;
                }

                // 2) already complete on disk from a prior session? -> skip.
                long long onDisk = DiskSize(dl, name);
                if (onDisk == (long long)f.size)
                {
                    std::wstringstream s;
                    s << L"  [SKIP]   " << tag << L"  already complete (" << onDisk << L" B)\n";
                    AppendBody(s.str());
                    continue;
                }

                // 3) fresh download.
                std::wstringstream s;
                s << L"  [START]  " << tag << L"  (" << f.size << L" B expected)\n";
                AppendBody(s.str());

                StorageFile^ dest = create_task(dl->CreateFileAsync(name, CreationCollisionOption::ReplaceExisting)).get();
                Uri^ uri = ref new Uri(ref new Platform::String((std::wstring(kBaseUrl) + f.name).c_str()));
                DownloadOperation^ op = downloader->CreateDownload(uri, dest);

                HRESULT err = S_OK;
                ULONGLONG t0 = GetTickCount64();
                AsyncStatus st = AwaitDownload(op->StartAsync(), tag, f.size, err);
                double secs = (GetTickCount64() - t0) / 1000.0;
                long long got = DiskSize(dl, name);
                double mbps = secs > 0 ? (got / (1024.0 * 1024.0)) / secs : 0.0;
                std::wstringstream d;
                d << L"  [DONE]   " << tag << L"  " << got << L"/" << f.size << L" B  "
                  << (st == AsyncStatus::Completed && got == (long long)f.size ? L"OK" : L"FAIL")
                  << L"  " << std::fixed << std::setprecision(1) << secs << L"s "
                  << std::fixed << std::setprecision(1) << mbps << L" MB/s";
                if (st != AsyncStatus::Completed)
                {
                    d << L"  hr=0x" << std::hex << (uint32_t)err;
                    auto info = op->GetResponseInformation();
                    if (info != nullptr) d << L"  http=" << std::dec << info->StatusCode;
                }
                d << L"\n";
                AppendBody(d.str());
            }

            // --- summary + integrity verdict ---
            std::wstringstream sum;
            sum << L"--- T4 summary ---\n";
            int present = 0; long long total = 0;
            for (auto& f : kFiles)
            {
                long long d = DiskSize(dl, ref new Platform::String(f.name));
                if (d == (long long)f.size) ++present;
                if (d > 0) total += d;
            }
            sum << L"[T4] files at exact expected size: " << present << L"/9   total on disk: "
                << total << L" B (" << MB(total) << L")\n";
            sum << L"[T4] LocalState model root: " << std::wstring(dl->Path->Data()) << L"\n";
            if (present == 9)
            {
                g_complete.store(true);
                sum << L"[T4] *** FULL FILE SET DOWNLOADED ON-DEVICE + SIZE-VERIFIED ***\n";
                sum << L"[T4] layout matches the loader: model.onnx + external model.onnx.data co-located in the model dir\n";
            }
            else
            {
                sum << L"[T4] INCOMPLETE - " << (9 - present)
                    << L" file(s) not yet at expected size (terminate+relaunch to exercise resume)\n";
            }
            AppendBody(sum.str());
        }
        catch (Platform::Exception^ e)
        {
            std::wstringstream s;
            s << L"[T4] EXCEPTION hr=0x" << std::hex << (uint32_t)e->HResult
              << L"  msg=" << std::wstring(e->Message->Data()) << L"\n";
            AppendBody(s.str());
        }
        catch (...)
        {
            AppendBody(L"[T4] non-WinRT exception in download worker\n");
        }
    }
}

void StartHfDownloadProbe(const std::wstring& reportSoFar)
{
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_prefix = reportSoFar;
    }
    // Run off the main thread so the dispatcher keeps pumping (2 GB takes minutes).
    g_work = ThreadPool::RunAsync(ref new WorkItemHandler(
        [](IAsyncAction^) { DownloadWorker(); }));
}

bool HfDownloadComplete()
{
    return g_complete.load();
}
