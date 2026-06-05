#include "pch.h"
#include "Probe.h"

#include <iomanip>
#include <fstream>

using namespace Windows::Storage;

namespace
{
    // DirectML's create entry point. We resolve it at runtime via GetProcAddress
    // (rather than linking DirectML.lib) precisely so we can answer the question
    // "is DirectML even present and loadable inside the UWP app container?".
    typedef HRESULT(WINAPI* PFN_DMLCREATEDEVICE)(ID3D12Device*, DML_CREATE_DEVICE_FLAGS, REFIID, void**);

    std::wstring Gib(uint64_t bytes)
    {
        std::wstringstream s;
        s << std::fixed << std::setprecision(2) << (double)bytes / (1024.0 * 1024.0 * 1024.0) << L" GiB";
        return s.str();
    }

    std::wstring Hr(HRESULT hr)
    {
        std::wstringstream s;
        s << L"0x" << std::hex << std::uppercase << std::setw(8) << std::setfill(L'0') << (uint32_t)hr;
        return s.str();
    }

    const wchar_t* FeatureLevelName(D3D_FEATURE_LEVEL fl)
    {
        switch (fl)
        {
        case D3D_FEATURE_LEVEL_12_2: return L"12_2";
        case D3D_FEATURE_LEVEL_12_1: return L"12_1";
        case D3D_FEATURE_LEVEL_12_0: return L"12_0";
        case D3D_FEATURE_LEVEL_11_1: return L"11_1";
        case D3D_FEATURE_LEVEL_11_0: return L"11_0";
        default: return L"<unknown>";
        }
    }

    // T1c instrumentation: snapshot the live video-memory budget for BOTH segment
    // groups. We sample this right before each OgaCreateModel attempt and again in
    // the failure branch, so the OOM is pinned to a real headroom number (Budget -
    // CurrentUsage) instead of the nominal 4.05 GiB. `adapter3` may be null (then we
    // emit a note rather than crash).
    void SnapshotVmem(IDXGIAdapter3* adapter3, std::wstringstream& r, const wchar_t* label)
    {
        if (!adapter3)
        {
            r << L"        [vmem " << label << L"] (no IDXGIAdapter3)\n";
            return;
        }
        DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonLocal{};
        adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
        adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocal);
        auto avail = [](const DXGI_QUERY_VIDEO_MEMORY_INFO& m) -> uint64_t {
            return m.Budget > m.CurrentUsage ? m.Budget - m.CurrentUsage : 0;
        };
        r << L"        [vmem " << label << L"]  LOCAL budget " << Gib(local.Budget)
          << L" / used " << Gib(local.CurrentUsage) << L" / free " << Gib(avail(local))
          << L"  ||  NONLOCAL budget " << Gib(nonLocal.Budget)
          << L" / used " << Gib(nonLocal.CurrentUsage) << L" / free " << Gib(avail(nonLocal)) << L"\n";
    }
}

std::wstring RunCapabilityProbe(unsigned int& statusOut)
{
    statusOut = PROBE_NONE;
    std::wstringstream r;
    r << L"================ Xbox AI Capability Probe ================\n";
    r << L"Goal: ask the actual console what the UWP 'game' container exposes.\n\n";

    // ---- 1. DXGI: enumerate the adapter -------------------------------------
    ComPtr<IDXGIFactory6> factory;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    r << L"[DXGI] CreateDXGIFactory2: " << Hr(hr) << L"\n";

    ComPtr<IDXGIAdapter1> adapter;
    if (SUCCEEDED(hr))
    {
        // Prefer the high-performance hardware adapter.
        for (UINT i = 0;
             factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
             ++i)
        {
            DXGI_ADAPTER_DESC1 d{};
            adapter->GetDesc1(&d);
            if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { adapter.Reset(); continue; }
            break;
        }

        if (adapter)
        {
            DXGI_ADAPTER_DESC1 d{};
            adapter->GetDesc1(&d);
            statusOut |= PROBE_DXGI_OK;
            r << L"[DXGI] Adapter            : " << d.Description << L"\n";
            r << L"[DXGI] VendorId/DeviceId  : " << std::hex << d.VendorId << L" / " << d.DeviceId << std::dec << L"\n";
            r << L"[DXGI] DedicatedVideoMem  : " << Gib(d.DedicatedVideoMemory) << L"\n";
            r << L"[DXGI] DedicatedSystemMem : " << Gib(d.DedicatedSystemMemory) << L"\n";
            r << L"[DXGI] SharedSystemMem    : " << Gib(d.SharedSystemMemory) << L"\n";
        }
        else
        {
            r << L"[DXGI] No hardware adapter found!\n";
        }
    }

    // ---- 2. D3D12 device + feature support ----------------------------------
    ComPtr<ID3D12Device> device;
    if (adapter)
    {
        hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
        r << L"\n[D3D12] D3D12CreateDevice (FL 11_0): " << Hr(hr) << L"\n";
        if (SUCCEEDED(hr))
        {
            statusOut |= PROBE_D3D12_OK;

            D3D_FEATURE_LEVEL levels[] = {
                D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
                D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
            D3D12_FEATURE_DATA_FEATURE_LEVELS fl{};
            fl.NumFeatureLevels = _countof(levels);
            fl.pFeatureLevelsRequested = levels;
            if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl))))
                r << L"[D3D12] Max feature level : " << FeatureLevelName(fl.MaxSupportedFeatureLevel) << L"\n";

            // Highest shader model: walk down until the runtime accepts one.
            for (D3D_SHADER_MODEL sm : { D3D_SHADER_MODEL(0x68), D3D_SHADER_MODEL(0x67), D3D_SHADER_MODEL(0x66),
                                         D3D_SHADER_MODEL(0x65), D3D_SHADER_MODEL_6_4, D3D_SHADER_MODEL_6_0 })
            {
                D3D12_FEATURE_DATA_SHADER_MODEL data{ sm };
                if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &data, sizeof(data))))
                {
                    r << L"[D3D12] Highest shader model: 0x" << std::hex << data.HighestShaderModel << std::dec << L"\n";
                    break;
                }
            }

            D3D12_FEATURE_DATA_ARCHITECTURE1 arch{};
            if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &arch, sizeof(arch))))
                r << L"[D3D12] UMA / CacheCoherentUMA: " << (arch.UMA ? L"yes" : L"no")
                  << L" / " << (arch.CacheCoherentUMA ? L"yes" : L"no") << L"\n";
        }
    }

    // ---- 3. Live video-memory budget (the "did we get the game pool?" test) -
    ComPtr<IDXGIAdapter3> adapter3;
    if (adapter && SUCCEEDED(adapter.As(&adapter3)))
    {
        DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonLocal{};
        adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
        adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonLocal);
        r << L"\n[BUDGET] Local Budget     : " << Gib(local.Budget) << L"  (CurrentUsage " << Gib(local.CurrentUsage) << L")\n";
        r << L"[BUDGET] NonLocal Budget  : " << Gib(nonLocal.Budget) << L"  (CurrentUsage " << Gib(nonLocal.CurrentUsage) << L")\n";
        if ((local.Budget + nonLocal.Budget) >= (4ull * 1024 * 1024 * 1024))
        {
            statusOut |= PROBE_BUDGET_BIG;
            r << L"[BUDGET] >= 4 GiB available -> looks like the GAME memory profile.\n";
        }
        else
        {
            r << L"[BUDGET] < 4 GiB -> looks like the APP profile; tag the package as a Game.\n";
        }
    }

    // ---- 4. DirectML: present & loadable inside the sandbox? ----------------
    r << L"\n[DML] LoadLibrary(DirectML.dll)...\n";
    HMODULE dml = LoadLibraryExW(L"DirectML.dll", nullptr, 0);
    if (!dml)
    {
        r << L"[DML] NOT loadable in container. GetLastError=" << GetLastError() << L"\n";
    }
    else
    {
        r << L"[DML] DirectML.dll loaded.\n";
        auto create = reinterpret_cast<PFN_DMLCREATEDEVICE>(GetProcAddress(dml, "DMLCreateDevice"));
        if (!create)
        {
            r << L"[DML] DMLCreateDevice export not found.\n";
        }
        else if (device)
        {
            ComPtr<IDMLDevice> dmlDevice;
            hr = create(device.Get(), DML_CREATE_DEVICE_FLAG_NONE, IID_PPV_ARGS(&dmlDevice));
            r << L"[DML] DMLCreateDevice: " << Hr(hr) << L"\n";
            if (SUCCEEDED(hr))
            {
                statusOut |= PROBE_DML_OK;
                // Best-effort: report the highest supported DirectML feature level.
                DML_FEATURE_LEVEL requested[] = {
                    DML_FEATURE_LEVEL_6_2, DML_FEATURE_LEVEL_6_1, DML_FEATURE_LEVEL_6_0,
                    DML_FEATURE_LEVEL_5_2, DML_FEATURE_LEVEL_5_1, DML_FEATURE_LEVEL_5_0,
                    DML_FEATURE_LEVEL_4_1, DML_FEATURE_LEVEL_4_0, DML_FEATURE_LEVEL_3_1,
                    DML_FEATURE_LEVEL_3_0, DML_FEATURE_LEVEL_2_1, DML_FEATURE_LEVEL_2_0,
                    DML_FEATURE_LEVEL_1_0 };
                DML_FEATURE_QUERY_FEATURE_LEVELS q{ _countof(requested), requested };
                DML_FEATURE_DATA_FEATURE_LEVELS data{};
                if (SUCCEEDED(dmlDevice->CheckFeatureSupport(DML_FEATURE_FEATURE_LEVELS, sizeof(q), &q, sizeof(data), &data)))
                    r << L"[DML] Max DirectML feature level: 0x" << std::hex << data.MaxSupportedFeatureLevel << std::dec << L"\n";
            }
        }
    }

    // ---- 5. WinML: in-box proxy for "will the ORT/WinML LLM stack init?" -----
    r << L"\n[WinML] LearningModelDevice(DirectX)...\n";
    try
    {
        using namespace Windows::AI::MachineLearning;
        auto wdev = ref new LearningModelDevice(LearningModelDeviceKind::DirectX);
        if (wdev)
        {
            statusOut |= PROBE_WINML_OK;
            r << L"[WinML] Created. AdapterId.LowPart=" << wdev->AdapterId.LowPart << L"\n";
        }
    }
    catch (Platform::Exception^ e)
    {
        r << L"[WinML] Exception: " << e->Message->Data() << L" (" << Hr(e->HResult) << L")\n";
    }

    r << L"\n================ Summary (status bits = 0x" << std::hex << statusOut << std::dec << L") ================\n";
    r << L"  DXGI adapter      : " << ((statusOut & PROBE_DXGI_OK)    ? L"OK" : L"FAIL") << L"\n";
    r << L"  D3D12 device      : " << ((statusOut & PROBE_D3D12_OK)   ? L"OK" : L"FAIL") << L"\n";
    r << L"  >=4GiB budget     : " << ((statusOut & PROBE_BUDGET_BIG) ? L"OK" : L"no")   << L"\n";
    r << L"  DirectML device   : " << ((statusOut & PROBE_DML_OK)     ? L"OK" : L"FAIL") << L"\n";
    r << L"  WinML DirectX dev : " << ((statusOut & PROBE_WINML_OK)   ? L"OK" : L"FAIL") << L"\n";
    return r.str();
}

void WriteReport(const std::wstring& report)
{
    // Mirror to debugger first so we get *something* even if file IO surprises us.
    OutputDebugStringW(report.c_str());
    OutputDebugStringW(L"\n");

    try
    {
        Platform::String^ path = ApplicationData::Current->LocalFolder->Path;
        std::wstring file = std::wstring(path->Data()) + L"\\probe-results.txt";

        int len = WideCharToMultiByte(CP_UTF8, 0, report.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::vector<char> utf8(len > 0 ? len : 1);
        WideCharToMultiByte(CP_UTF8, 0, report.c_str(), -1, utf8.data(), len, nullptr, nullptr);

        // CreateFile2 is the UWP-sanctioned Win32 file API (the CRT's fopen/
        // ofstream is frequently blocked inside the app container).
        HANDLE h = CreateFile2(file.c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS, nullptr);
        if (h != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            if (len > 1) WriteFile(h, utf8.data(), (DWORD)(len - 1), &written, nullptr); // drop trailing NUL
            CloseHandle(h);
        }
        else
        {
            wchar_t msg[128];
            swprintf_s(msg, L"[WriteReport] CreateFile2 failed, GetLastError=%lu\n", GetLastError());
            OutputDebugStringW(msg);
        }
    }
    catch (...)
    {
        OutputDebugStringW(L"[WriteReport] exception writing probe-results.txt\n");
    }
}

// ---------------------------------------------------------------------------
// T1 gate: does the stock ORT-GenAI native stack load + init in the container?
// ---------------------------------------------------------------------------
std::wstring RunOrtGenAIProbe(unsigned int& statusInOut, const std::wstring& reportSoFar)
{
    std::wstringstream r;
    r << L"\n\n================ T1: stock ORT-GenAI load + init gate ================\n";
    r << L"Stack: onnxruntime-genai 0.6.0 / onnxruntime 1.20.1 / DirectML 1.15.2 (x64-win),\n";
    r << L"loaded by full path from .\\redist; then OgaCreateModel on the staged INT4 model.\n\n";

    // --- locate the package install dir + the bundled redist subfolder ---
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir(exePath);
    size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir.resize(slash);
    std::wstring redist = dir + L"\\redist";
    r << L"[ORT] install dir : " << dir << L"\n";
    r << L"[ORT] redist dir  : " << redist << L"\n\n";

    // --- load the 3 DLLs by full path, in dependency order. The flags make each
    //     DLL's own directory (redist) searched for ITS dependencies, and loading
    //     DirectML/onnxruntime first puts them in the module list so ORT's later
    //     internal bare-name LoadLibrary("DirectML.dll") reuses the loaded copy. ---
    // --- T1b diagnostic: probe each of onnxruntime.dll's dependencies (beyond what
    //     DirectML.dll already proved present) by bare name, to NAME whichever is the
    //     missing dependency behind the err-126 load failure. ---
    r << L"[ORT] dependency probe (onnxruntime.dll imports, bare-name LoadLibrary):\n";
    const wchar_t* deps[] = {
        L"dxcore.dll", L"ole32.dll", L"OLEAUT32.dll", L"ADVAPI32.dll",
        L"VCRUNTIME140.dll", L"VCRUNTIME140_1.dll", L"MSVCP140.dll", L"MSVCP140_1.dll",
        L"api-ms-win-core-path-l1-1-0.dll",
    };
    for (auto dep : deps)
    {
        HMODULE m = LoadLibraryExW(dep, nullptr, 0);
        if (m) { r << L"        " << dep << L" : OK\n"; }
        else   { r << L"        " << dep << L" : FAIL err=" << GetLastError() << L"\n"; }
    }
    r << L"\n";

    const DWORD kFlags = LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR;

    // --- preload the bundled DESKTOP VC runtime by full path (the container only has
    //     the _app CRT variants, so stock ORT's vcruntime140/msvcp140 imports must be
    //     satisfied from our redist). Put them in the module list before onnxruntime. ---
    r << L"[ORT] preloading bundled desktop VC runtime from redist:\n";
    for (auto crt : { L"vcruntime140.dll", L"vcruntime140_1.dll", L"msvcp140.dll", L"msvcp140_1.dll" })
    {
        std::wstring full = redist + L"\\" + crt;
        HMODULE m = LoadLibraryExW(full.c_str(), nullptr, kFlags);
        r << L"        " << crt << L" : " << (m ? L"OK" : L"FAIL") << L"\n";
    }
    r << L"\n";

    struct DllSpec { const wchar_t* name; HMODULE h; };
    DllSpec dlls[] = {
        { L"DirectML.dll",          nullptr },
        { L"onnxruntime.dll",       nullptr },
        { L"onnxruntime-genai.dll", nullptr },
    };
    bool allLoaded = true;
    for (auto& d : dlls)
    {
        std::wstring full = redist + L"\\" + d.name;
        d.h = LoadLibraryExW(full.c_str(), nullptr, kFlags);
        if (d.h)
        {
            r << L"[ORT] LoadLibrary " << d.name << L" : OK\n";
        }
        else
        {
            DWORD e = GetLastError();
            r << L"[ORT] LoadLibrary " << d.name << L" : FAIL  GetLastError=" << e
              << L" (0x" << std::hex << std::uppercase << e << std::dec << L")\n";
            allLoaded = false;
            break; // first failure is the datum; a missing dep cascades
        }
    }
    HMODULE genai = dlls[2].h;

    // --- resolve the OGA C API entry points (x64 => single calling convention,
    //     so plain function-pointer typedefs match OGA_API_CALL). ---
    struct OgaResult; struct OgaModel; struct OgaConfig;
    struct OgaTokenizer; struct OgaTokenizerStream; struct OgaSequences;
    struct OgaGeneratorParams; struct OgaGenerator;
    using PFN_CreateModel   = OgaResult* (*)(const char*, OgaModel**);
    using PFN_GetError      = const char* (*)(const OgaResult*);
    using PFN_DestroyResult = void (*)(OgaResult*);
    using PFN_DestroyModel  = void (*)(OgaModel*);
    using PFN_SetLogBool    = OgaResult* (*)(const char*, bool);
    using PFN_Shutdown      = void (*)();
    // T1c config-overlay APIs (present in onnxruntime-genai 0.6.0): build a config
    // in-memory so we can swap the execution provider (DML <-> CPU) WITHOUT restaging
    // the 2 GB model — the CPU run is the capacity-isolation floor test.
    using PFN_CreateConfig        = OgaResult* (*)(const char*, OgaConfig**);
    using PFN_ConfigClearProviders= OgaResult* (*)(OgaConfig*);
    using PFN_ConfigAppendProvider= OgaResult* (*)(OgaConfig*, const char*);
    using PFN_ConfigSetProviderOpt= OgaResult* (*)(OgaConfig*, const char*, const char*, const char*);
    using PFN_CreateModelFromConfig=OgaResult* (*)(const OgaConfig*, OgaModel**);
    using PFN_DestroyConfig       = void (*)(OgaConfig*);
    // T2 generation API (onnxruntime-genai 0.6.0 AppendTokens flow): tokenize a
    // prompt, build a generator, then GenerateNextToken to decode tokens on the GPU.
    using PFN_CreateTokenizer  = OgaResult* (*)(const OgaModel*, OgaTokenizer**);
    using PFN_DestroyTokenizer = void (*)(OgaTokenizer*);
    using PFN_CreateSequences  = OgaResult* (*)(OgaSequences**);
    using PFN_DestroySequences = void (*)(OgaSequences*);
    using PFN_TokenizerEncode  = OgaResult* (*)(const OgaTokenizer*, const char*, OgaSequences*);
    using PFN_CreateTokStream  = OgaResult* (*)(const OgaTokenizer*, OgaTokenizerStream**);
    using PFN_DestroyTokStream = void (*)(OgaTokenizerStream*);
    using PFN_TokStreamDecode  = OgaResult* (*)(OgaTokenizerStream*, int32_t, const char**);
    using PFN_CreateGenParams  = OgaResult* (*)(const OgaModel*, OgaGeneratorParams**);
    using PFN_DestroyGenParams = void (*)(OgaGeneratorParams*);
    using PFN_GenParamsSetNum  = OgaResult* (*)(OgaGeneratorParams*, const char*, double);
    using PFN_CreateGenerator  = OgaResult* (*)(const OgaModel*, const OgaGeneratorParams*, OgaGenerator**);
    using PFN_DestroyGenerator = void (*)(OgaGenerator*);
    using PFN_GenAppendTokSeqs = OgaResult* (*)(OgaGenerator*, const OgaSequences*);
    using PFN_GenNextToken     = OgaResult* (*)(OgaGenerator*);
    using PFN_GenIsDone        = bool (*)(const OgaGenerator*);
    using PFN_GenSeqCount      = size_t (*)(const OgaGenerator*, size_t);
    using PFN_GenSeqData       = const int32_t* (*)(const OgaGenerator*, size_t);

    PFN_CreateModel   pCreateModel   = nullptr;
    PFN_GetError      pGetError      = nullptr;
    PFN_DestroyResult pDestroyResult = nullptr;
    PFN_DestroyModel  pDestroyModel  = nullptr;
    PFN_SetLogBool    pSetLogBool    = nullptr;
    PFN_Shutdown      pShutdown      = nullptr;
    PFN_CreateConfig         pCreateConfig         = nullptr;
    PFN_ConfigClearProviders pConfigClearProviders = nullptr;
    PFN_ConfigAppendProvider pConfigAppendProvider = nullptr;
    PFN_ConfigSetProviderOpt pConfigSetProviderOpt = nullptr;
    PFN_CreateModelFromConfig pCreateModelFromConfig = nullptr;
    PFN_DestroyConfig        pDestroyConfig        = nullptr;
    PFN_CreateTokenizer  pCreateTokenizer  = nullptr;
    PFN_DestroyTokenizer pDestroyTokenizer = nullptr;
    PFN_CreateSequences  pCreateSequences  = nullptr;
    PFN_DestroySequences pDestroySequences = nullptr;
    PFN_TokenizerEncode  pTokenizerEncode  = nullptr;
    PFN_CreateTokStream  pCreateTokStream  = nullptr;
    PFN_DestroyTokStream pDestroyTokStream = nullptr;
    PFN_TokStreamDecode  pTokStreamDecode  = nullptr;
    PFN_CreateGenParams  pCreateGenParams  = nullptr;
    PFN_DestroyGenParams pDestroyGenParams = nullptr;
    PFN_GenParamsSetNum  pGenParamsSetNum  = nullptr;
    PFN_CreateGenerator  pCreateGenerator  = nullptr;
    PFN_DestroyGenerator pDestroyGenerator = nullptr;
    PFN_GenAppendTokSeqs pGenAppendTokSeqs = nullptr;
    PFN_GenNextToken     pGenNextToken     = nullptr;
    PFN_GenIsDone        pGenIsDone        = nullptr;
    PFN_GenSeqCount      pGenSeqCount      = nullptr;
    PFN_GenSeqData       pGenSeqData       = nullptr;

    bool allSymbols = false;
    if (genai)
    {
        pCreateModel   = reinterpret_cast<PFN_CreateModel>(GetProcAddress(genai, "OgaCreateModel"));
        pGetError      = reinterpret_cast<PFN_GetError>(GetProcAddress(genai, "OgaResultGetError"));
        pDestroyResult = reinterpret_cast<PFN_DestroyResult>(GetProcAddress(genai, "OgaDestroyResult"));
        pDestroyModel  = reinterpret_cast<PFN_DestroyModel>(GetProcAddress(genai, "OgaDestroyModel"));
        pSetLogBool    = reinterpret_cast<PFN_SetLogBool>(GetProcAddress(genai, "OgaSetLogBool"));
        pShutdown      = reinterpret_cast<PFN_Shutdown>(GetProcAddress(genai, "OgaShutdown"));
        pCreateConfig          = reinterpret_cast<PFN_CreateConfig>(GetProcAddress(genai, "OgaCreateConfig"));
        pConfigClearProviders  = reinterpret_cast<PFN_ConfigClearProviders>(GetProcAddress(genai, "OgaConfigClearProviders"));
        pConfigAppendProvider  = reinterpret_cast<PFN_ConfigAppendProvider>(GetProcAddress(genai, "OgaConfigAppendProvider"));
        pConfigSetProviderOpt  = reinterpret_cast<PFN_ConfigSetProviderOpt>(GetProcAddress(genai, "OgaConfigSetProviderOption"));
        pCreateModelFromConfig = reinterpret_cast<PFN_CreateModelFromConfig>(GetProcAddress(genai, "OgaCreateModelFromConfig"));
        pDestroyConfig         = reinterpret_cast<PFN_DestroyConfig>(GetProcAddress(genai, "OgaDestroyConfig"));
        pCreateTokenizer  = reinterpret_cast<PFN_CreateTokenizer>(GetProcAddress(genai, "OgaCreateTokenizer"));
        pDestroyTokenizer = reinterpret_cast<PFN_DestroyTokenizer>(GetProcAddress(genai, "OgaDestroyTokenizer"));
        pCreateSequences  = reinterpret_cast<PFN_CreateSequences>(GetProcAddress(genai, "OgaCreateSequences"));
        pDestroySequences = reinterpret_cast<PFN_DestroySequences>(GetProcAddress(genai, "OgaDestroySequences"));
        pTokenizerEncode  = reinterpret_cast<PFN_TokenizerEncode>(GetProcAddress(genai, "OgaTokenizerEncode"));
        pCreateTokStream  = reinterpret_cast<PFN_CreateTokStream>(GetProcAddress(genai, "OgaCreateTokenizerStream"));
        pDestroyTokStream = reinterpret_cast<PFN_DestroyTokStream>(GetProcAddress(genai, "OgaDestroyTokenizerStream"));
        pTokStreamDecode  = reinterpret_cast<PFN_TokStreamDecode>(GetProcAddress(genai, "OgaTokenizerStreamDecode"));
        pCreateGenParams  = reinterpret_cast<PFN_CreateGenParams>(GetProcAddress(genai, "OgaCreateGeneratorParams"));
        pDestroyGenParams = reinterpret_cast<PFN_DestroyGenParams>(GetProcAddress(genai, "OgaDestroyGeneratorParams"));
        pGenParamsSetNum  = reinterpret_cast<PFN_GenParamsSetNum>(GetProcAddress(genai, "OgaGeneratorParamsSetSearchNumber"));
        pCreateGenerator  = reinterpret_cast<PFN_CreateGenerator>(GetProcAddress(genai, "OgaCreateGenerator"));
        pDestroyGenerator = reinterpret_cast<PFN_DestroyGenerator>(GetProcAddress(genai, "OgaDestroyGenerator"));
        pGenAppendTokSeqs = reinterpret_cast<PFN_GenAppendTokSeqs>(GetProcAddress(genai, "OgaGenerator_AppendTokenSequences"));
        pGenNextToken     = reinterpret_cast<PFN_GenNextToken>(GetProcAddress(genai, "OgaGenerator_GenerateNextToken"));
        pGenIsDone        = reinterpret_cast<PFN_GenIsDone>(GetProcAddress(genai, "OgaGenerator_IsDone"));
        pGenSeqCount      = reinterpret_cast<PFN_GenSeqCount>(GetProcAddress(genai, "OgaGenerator_GetSequenceCount"));
        pGenSeqData       = reinterpret_cast<PFN_GenSeqData>(GetProcAddress(genai, "OgaGenerator_GetSequenceData"));
        r << L"[ORT] GetProcAddress OgaCreateModel    : " << (pCreateModel ? L"OK" : L"NULL") << L"\n";
        r << L"[ORT] GetProcAddress OgaResultGetError : " << (pGetError ? L"OK" : L"NULL") << L"\n";
        r << L"[ORT] GetProcAddress OgaDestroyModel   : " << (pDestroyModel ? L"OK" : L"NULL") << L"\n";
        r << L"[ORT] GetProcAddress OgaCreateConfig / FromConfig : "
          << (pCreateConfig ? L"OK" : L"NULL") << L" / " << (pCreateModelFromConfig ? L"OK" : L"NULL") << L"\n";
        r << L"[ORT] GetProcAddress T2 (Tokenizer/Generator/NextToken) : "
          << (pCreateTokenizer ? L"OK" : L"NULL") << L" / "
          << (pCreateGenerator ? L"OK" : L"NULL") << L" / "
          << (pGenNextToken ? L"OK" : L"NULL") << L"\n";
        allSymbols = pCreateModel && pGetError && pDestroyResult && pDestroyModel;
    }

    if (allLoaded && allSymbols)
    {
        statusInOut |= PROBE_ORT_LOADED;
        r << L"[ORT] => all DLLs loaded + OGA symbols resolved.\n";
    }
    else
    {
        r << L"[ORT] => stack NOT fully loaded; will skip OgaCreateModel.\n";
    }

    // --- find + enumerate the staged model (verifies the staging step worked) ---
    std::wstring modelDir;
    try {
        modelDir = std::wstring(ApplicationData::Current->LocalFolder->Path->Data())
                 + L"\\models\\phi3-mini-int4";
    } catch (...) {}
    r << L"\n[ORT] model dir   : " << modelDir << L"\n";

    // Create the model dir tree so it can be staged into via WDP upload (the Device
    // Portal file API won't create missing parent folders; the app must).
    try {
        std::wstring local = std::wstring(ApplicationData::Current->LocalFolder->Path->Data());
        CreateDirectoryW((local + L"\\models").c_str(), nullptr);
        CreateDirectoryW((local + L"\\models\\phi3-mini-int4").c_str(), nullptr);
    } catch (...) {}

    bool haveConfig = false;
    if (!modelDir.empty())
    {
        WIN32_FIND_DATAW fd{};
        HANDLE hf = FindFirstFileW((modelDir + L"\\*").c_str(), &fd);
        if (hf != INVALID_HANDLE_VALUE)
        {
            r << L"[ORT] staged files:\n";
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                uint64_t sz = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                r << L"        " << fd.cFileName << L"  (" << sz << L" bytes)\n";
                if (wcscmp(fd.cFileName, L"genai_config.json") == 0) haveConfig = true;
            } while (FindNextFileW(hf, &fd));
            FindClose(hf);
        }
        else
        {
            r << L"[ORT] (model dir not found / empty - not staged yet)\n";
        }
    }

    // --- T1c: acquire an IDXGIAdapter3 so each init attempt can be bracketed with a
    //     live video-memory snapshot (the OOM gets pinned to real headroom). ---
    ComPtr<IDXGIAdapter3> adapter3;
    {
        ComPtr<IDXGIFactory6> f;
        if (SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&f))))
        {
            ComPtr<IDXGIAdapter1> a;
            for (UINT i = 0;
                 f->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) != DXGI_ERROR_NOT_FOUND;
                 ++i)
            {
                DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
                if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { a.Reset(); continue; }
                break;
            }
            if (a) a.As(&adapter3);
        }
    }

    // small helpers shared by the init + decode attempts
    auto toUtf8 = [](const std::wstring& w) -> std::string {
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::vector<char> v(n > 0 ? n : 1);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, v.data(), n, nullptr, nullptr);
        return std::string(v.data());
    };
    auto errStr = [&](struct OgaResult* res) -> std::wstring {
        const char* e = (res && pGetError) ? pGetError(res) : "(null OgaResult / no error string)";
        int wn = MultiByteToWideChar(CP_UTF8, 0, e ? e : "", -1, nullptr, 0);
        std::vector<wchar_t> w(wn > 0 ? wn : 1);
        MultiByteToWideChar(CP_UTF8, 0, e ? e : "", -1, w.data(), wn);
        return std::wstring(w.data());
    };
    const std::string modelU8 = toUtf8(modelDir);

    if (pSetLogBool) pSetLogBool("enabled", true); // verbose ORT logging (stdout; harmless)

    // T2 timing (QueryPerformanceCounter) + a LOCAL-segment "used bytes" reader used
    // for the GPU-residency / CPU-fallback decision.
    LARGE_INTEGER qpf{}; QueryPerformanceFrequency(&qpf);
    auto nowTick = []() -> long long { LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return t.QuadPart; };
    auto elapsedMs = [&](long long a, long long b) -> double {
        return qpf.QuadPart ? (double)(b - a) * 1000.0 / (double)qpf.QuadPart : 0.0;
    };
    auto localUsed = [&](IDXGIAdapter3* a) -> uint64_t {
        if (!a) return 0;
        DXGI_QUERY_VIDEO_MEMORY_INFO m{};
        a->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &m);
        return m.CurrentUsage;
    };

    if (allLoaded && allSymbols && haveConfig)
    {
        // === T1 (regression): OgaCreateModel on DML with the pristine genai_config.
        //     Re-proves the T1c gate AND keeps the model alive so T2 can decode on it.
        //     T1c's Attempt-B (config patch) / Attempt-C (CPU floor) diagnostics are
        //     retired now that the gate passed — one clean DML load maximises KV-cache
        //     headroom and avoids extra load/destroy churn on the fragmentation-
        //     sensitive 2 GB commit (see FINDINGS Run 6). ===
        r << L"\n--- T1 init: OgaCreateModel (DML, pristine genai_config) ---\n";
        r << L"[ORT] >>> CHECKPOINT; about to call OgaCreateModel ...\n";
        SnapshotVmem(adapter3.Get(), r, L"init pre ");
        WriteReport(reportSoFar + r.str());

        OgaModel* model = nullptr;
        OgaResult* res = pCreateModel(modelU8.c_str(), &model);
        if (res != nullptr || model == nullptr)
        {
            r << L"[ORT] T1 init: FAILED\n[ORT] error: " << errStr(res) << L"\n";
            SnapshotVmem(adapter3.Get(), r, L"init fail");
            if (res && pDestroyResult) pDestroyResult(res);
        }
        else
        {
            statusInOut |= PROBE_ORT_INIT;
            r << L"[ORT] T1 init: *** SUCCESS *** - weights committed on DML.\n";
            SnapshotVmem(adapter3.Get(), r, L"init ok  ");

            // ================= T2: forward pass + token decode (GPU) =================
            r << L"\n================ T2: forward pass + token decode (GPU) ================\n";

            // GPU-confirmation evidence #1: which execution provider does the staged
            // genai_config request? a 'dml' entry => the DirectML EP is selected.
            {
                std::wstring cfgPath = modelDir + L"\\genai_config.json";
                std::string cfg;
                HANDLE h = CreateFile2(cfgPath.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
                if (h != INVALID_HANDLE_VALUE)
                {
                    LARGE_INTEGER sz{}; GetFileSizeEx(h, &sz);
                    cfg.resize((size_t)sz.QuadPart);
                    DWORD rd = 0; if (!cfg.empty()) ReadFile(h, &cfg[0], (DWORD)cfg.size(), &rd, nullptr);
                    cfg.resize(rd); CloseHandle(h);
                }
                bool hasDml  = cfg.find("\"dml\"") != std::string::npos || cfg.find("DmlExecutionProvider") != std::string::npos;
                bool hasCuda = cfg.find("\"cuda\"") != std::string::npos;
                r << L"[T2] genai_config execution provider: "
                  << (hasDml ? L"dml (DirectML)" : (hasCuda ? L"cuda" : L"cpu/unknown")) << L"\n";
            }

            bool t2syms = pCreateTokenizer && pCreateSequences && pTokenizerEncode &&
                          pCreateGenParams && pCreateGenerator && pGenAppendTokSeqs &&
                          pGenNextToken && pGenSeqCount && pGenSeqData;
            r << L"[T2] generation symbols: " << (t2syms ? L"all resolved" : L"MISSING - cannot decode") << L"\n";

            if (t2syms)
            {
                // Phi-3-mini-4k-instruct chat template; a tiny prompt keeps prefill cheap.
                const char* prompt =
                    "<|user|>\nWhat is the capital of France? Answer in one word.<|end|>\n<|assistant|>\n";

                OgaTokenizer* tok = nullptr;
                OgaResult* tr = pCreateTokenizer(model, &tok);
                if (tr != nullptr || tok == nullptr)
                {
                    r << L"[T2] OgaCreateTokenizer FAILED: " << errStr(tr) << L"\n";
                    if (tr && pDestroyResult) pDestroyResult(tr);
                }
                else
                {
                    OgaSequences* seq = nullptr;
                    pCreateSequences(&seq);
                    OgaResult* er = pTokenizerEncode(tok, prompt, seq);
                    if (er) { r << L"[T2] OgaTokenizerEncode FAILED: " << errStr(er) << L"\n"; if (pDestroyResult) pDestroyResult(er); }

                    OgaTokenizerStream* stream = nullptr;
                    if (pCreateTokStream) pCreateTokStream(tok, &stream);

                    OgaGeneratorParams* params = nullptr;
                    OgaResult* pr = pCreateGenParams(model, &params);
                    if (pr) { r << L"[T2] OgaCreateGeneratorParams FAILED: " << errStr(pr) << L"\n"; if (pDestroyResult) pDestroyResult(pr); }
                    const int kMaxNew = 24;             // token budget for the tokens/s run
                    if (pGenParamsSetNum && params) pGenParamsSetNum(params, "max_length", 256.0);

                    OgaGenerator* gen = nullptr;
                    OgaResult* gr = params ? pCreateGenerator(model, params, &gen) : nullptr;
                    if (gr != nullptr || gen == nullptr)
                    {
                        r << L"[T2] OgaCreateGenerator FAILED: " << errStr(gr) << L"\n";
                        if (gr && pDestroyResult) pDestroyResult(gr);
                    }
                    else
                    {
                        OgaResult* ar = pGenAppendTokSeqs(gen, seq);
                        if (ar) { r << L"[T2] AppendTokenSequences FAILED: " << errStr(ar) << L"\n"; if (pDestroyResult) pDestroyResult(ar); }
                        size_t promptLen = pGenSeqCount(gen, 0);
                        r << L"[T2] prompt tokens (post-append): " << promptLen << L"\n";

                        SnapshotVmem(adapter3.Get(), r, L"decode pre");
                        r << L"[T2] >>> CHECKPOINT; about to GenerateNextToken (first / prefill) ...\n";
                        WriteReport(reportSoFar + r.str());

                        // ---- first token = prompt prefill + 1 decode step (TTFT) ----
                        long long t0 = nowTick();
                        OgaResult* nr = pGenNextToken(gen);
                        long long t1 = nowTick();
                        if (nr != nullptr)
                        {
                            r << L"[T2] first GenerateNextToken FAILED\n[T2] error: " << errStr(nr) << L"\n";
                            SnapshotVmem(adapter3.Get(), r, L"decode fail");
                            if (pDestroyResult) pDestroyResult(nr);
                        }
                        else
                        {
                            statusInOut |= PROBE_ORT_DECODE;
                            size_t cnt = pGenSeqCount(gen, 0);
                            const int32_t* sd = pGenSeqData(gen, 0);
                            int32_t firstTok = (sd && cnt > promptLen) ? sd[promptLen] : -1;
                            r << L"[T2] *** FIRST TOKEN DECODED *** id=" << firstTok
                              << L"  TTFT(prefill+1)=" << std::fixed << std::setprecision(1)
                              << elapsedMs(t0, t1) << L" ms\n";

                            std::wstring text;
                            auto appendPiece = [&](int32_t tk) {
                                if (!stream || !pTokStreamDecode) return;
                                const char* piece = nullptr;
                                OgaResult* dr = pTokStreamDecode(stream, tk, &piece);
                                if (!dr && piece)
                                {
                                    int wn = MultiByteToWideChar(CP_UTF8, 0, piece, -1, nullptr, 0);
                                    std::vector<wchar_t> w(wn > 0 ? wn : 1);
                                    MultiByteToWideChar(CP_UTF8, 0, piece, -1, w.data(), wn);
                                    text += w.data();
                                }
                                else if (dr && pDestroyResult) pDestroyResult(dr);
                            };
                            appendPiece(firstTok);

                            SnapshotVmem(adapter3.Get(), r, L"decode t1");
                            uint64_t usedMid = localUsed(adapter3.Get());

                            // ---- steady-state decode loop for tokens/s ----
                            long long ts = nowTick();
                            int generated = 1;
                            while (generated < kMaxNew && !(pGenIsDone && pGenIsDone(gen)))
                            {
                                OgaResult* lr = pGenNextToken(gen);
                                if (lr) { r << L"[T2] decode step FAILED: " << errStr(lr) << L"\n"; if (pDestroyResult) pDestroyResult(lr); break; }
                                size_t c2 = pGenSeqCount(gen, 0);
                                const int32_t* d2 = pGenSeqData(gen, 0);
                                int32_t tk = (d2 && c2 > 0) ? d2[c2 - 1] : -1;
                                appendPiece(tk);
                                ++generated;
                            }
                            long long te = nowTick();
                            double decMs = elapsedMs(ts, te);
                            int steady = generated - 1; // tokens generated after the first
                            double tps = (decMs > 0.0 && steady > 0) ? (steady * 1000.0 / decMs) : 0.0;

                            r << L"[T2] generated " << generated << L" tokens total (1 prefill + "
                              << steady << L" steady)\n";
                            r << L"[T2] steady-state decode: " << steady << L" tok in "
                              << std::setprecision(1) << decMs << L" ms = "
                              << std::setprecision(2) << tps << L" tok/s\n";
                            r << L"[T2] decoded text: \"" << text << L"\"\n";

                            SnapshotVmem(adapter3.Get(), r, L"decode end");

                            // GPU-confirmation evidence #2: the INT4 weights must stay resident
                            // on the adapter LOCAL segment through decode. ~2 GB resident == the
                            // weights live on the GPU (DML); a CPU-EP fallback showed 0.00 used
                            // in the CPU floor of Run 5/6.
                            double midGiB = usedMid / (1024.0 * 1024.0 * 1024.0);
                            bool gpu = usedMid >= (1500ull * 1024 * 1024); // >= ~1.5 GiB
                            r << L"[T2] GPU LOCAL used during decode: " << std::setprecision(2) << midGiB
                              << L" GiB => "
                              << (gpu ? L"DirectML GPU execution CONFIRMED (weights resident on adapter)"
                                      : L"WARNING: low residency - possible CPU fallback") << L"\n";
                            if (gpu) statusInOut |= PROBE_ORT_GPU;
                        }

                        if (pDestroyGenerator) pDestroyGenerator(gen);
                    }

                    if (stream && pDestroyTokStream) pDestroyTokStream(stream);
                    if (params && pDestroyGenParams) pDestroyGenParams(params);
                    if (seq && pDestroySequences) pDestroySequences(seq);
                    if (pDestroyTokenizer) pDestroyTokenizer(tok);
                }
            }

            if (pDestroyModel) pDestroyModel(model);
        }

        if (pShutdown) pShutdown();
    }
    else if (allLoaded && allSymbols && !haveConfig)
    {
        r << L"[ORT] genai_config.json not staged - DLL-load half done; stage model + re-launch for init+decode.\n";
    }

    r << L"\n[ORT] T2 result: DLLs " << ((statusInOut & PROBE_ORT_LOADED) ? L"LOADED" : L"NOT-loaded")
      << L" | init " << ((statusInOut & PROBE_ORT_INIT) ? L"INIT-OK" : L"not-init")
      << L" | decode " << ((statusInOut & PROBE_ORT_DECODE) ? L"TOKEN-OK" : L"no-token")
      << L" | GPU " << ((statusInOut & PROBE_ORT_GPU) ? L"CONFIRMED" : L"unconfirmed")
      << L"  (see T2 section above + vmem snapshots)\n";
    return r.str();
}
