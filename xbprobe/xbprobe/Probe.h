#pragma once
#include <string>

// Status bits, OR'd together. Used to pick the on-screen status color.
enum ProbeStatus : unsigned int
{
    PROBE_NONE        = 0,
    PROBE_DXGI_OK     = 1 << 0,  // enumerated a hardware adapter
    PROBE_D3D12_OK    = 1 << 1,  // created a D3D12 device
    PROBE_BUDGET_BIG  = 1 << 2,  // video-memory budget >= 4 GiB (i.e. "game" profile, not "app")
    PROBE_DML_OK      = 1 << 3,  // DirectML.dll loaded AND IDMLDevice created in-sandbox
    PROBE_WINML_OK    = 1 << 4,  // WinML LearningModelDevice(DirectX) constructed
    PROBE_ORT_LOADED  = 1 << 5,  // all 3 stock ORT-GenAI DLLs LoadLibrary'd + OGA symbols resolved
    PROBE_ORT_INIT    = 1 << 6,  // OgaCreateModel succeeded against the staged INT4 model (THE gate)
    PROBE_ORT_DECODE  = 1 << 7,  // T2: OgaGenerator_GenerateNextToken produced >=1 token end-to-end
    PROBE_ORT_GPU     = 1 << 8,  // T2: weights stayed GPU-resident during decode => DirectML, not CPU fallback
    PROBE_LAN_BOUND   = 1 << 9,  // T3: StreamSocketListener bound a fixed LAN port (inbound socket open)
    PROBE_LAN_SERVED  = 1 << 10, // T3: served >=1 inbound HTTP request from another LAN machine
    PROBE_HF_STARTED  = 1 << 11, // T4: a BackgroundDownloader transfer was started/attached
    PROBE_HF_RESUMED  = 1 << 12, // T4: re-attached an interrupted transfer from a prior session
    PROBE_HF_COMPLETE = 1 << 13, // T4: full Phi-3-mini INT4 file set on-device at exact expected sizes
};

// Runs every capability check. Returns a full human-readable report and
// sets `statusOut` to the OR of the ProbeStatus bits above.
std::wstring RunCapabilityProbe(unsigned int& statusOut);

// T1+T2: package the stock-release ORT-GenAI native stack (onnxruntime-genai.dll +
// onnxruntime.dll + DirectML.dll, loaded by full path from .\redist), resolve the OGA
// C API, then against the SMB/WDP-staged INT4 model in LocalState\models\phi3-mini-int4:
//   T1 — OgaCreateModel (DML) commits the weights to the GPU (PROBE_ORT_INIT).
//   T2 — tokenize a prompt, OgaCreateGenerator, GenerateNextToken to decode tokens;
//        records first token + TTFT, steady-state tokens/s, and the working-set vmem
//        snapshots. Sets PROBE_ORT_DECODE (>=1 token) and PROBE_ORT_GPU (weights stayed
//        GPU-resident through decode => DirectML, not a CPU-EP fallback).
// `reportSoFar` is the capability report; CHECKPOINTs are flushed to disk before each
// crash-prone OGA call (OgaCreateModel, first GenerateNextToken) so a hard fault still
// leaves a breadcrumb. Returns this stage's report section.
std::wstring RunOrtGenAIProbe(unsigned int& statusInOut, const std::wstring& reportSoFar);

// T3: LAN inbound serving reachability. De-risks ADR-0002's load-bearing
// assumption — can a Game-classified UWP app accept inbound LAN TCP reachable
// from the dev PC? Declares `privateNetworkClientServer` in the manifest, opens a
// WinRT `StreamSocketListener` on a fixed port (8080), and answers a trivial
// HTTP/1.1 `GET` with `200 OK`. Records the bound port + any bind/listen error
// into the report, then LIVE-UPDATES the report file from the ConnectionReceived
// handler as inbound requests arrive (so a `curl` from the dev PC leaves a trace we
// can pull via WDP). Sets PROBE_LAN_BOUND on a successful bind and PROBE_LAN_SERVED
// once a real inbound request is served. `reportSoFar` is the report up to this
// point (the handler rewrites the whole file, so it needs the full prefix).
std::wstring RunLanServeProbe(unsigned int& statusInOut, const std::wstring& reportSoFar);

// True once the T3 listener has served at least one inbound request. Polled by the
// render loop to flip the on-screen status colour (bound -> served).
bool LanServedAtLeastOnce();

// T4: on-device Hugging Face download spike. Kicks off a BackgroundDownloader fetch
// of the Phi-3-mini INT4 file set into LocalState\models\phi3-mini-int4-dl\ on a
// ThreadPool worker (NON-blocking — the 2 GB blob takes minutes; we must not stall
// the dispatcher). On a fresh launch it starts all 9 transfers; if a prior launch
// was interrupted, GetCurrentDownloadsAsync re-attaches and RESUMES the partial
// transfer instead of restarting. Live-updates the LocalState report with per-file
// progress/size + a final integrity verdict. `reportSoFar` is the report prefix the
// worker rewrites on top of.
void StartHfDownloadProbe(const std::wstring& reportSoFar);

// True once the full T4 file set is on-device at exact expected sizes. Polled by the
// render loop to flip the on-screen status colour while the download runs.
bool HfDownloadComplete();

// Writes the report to the app's LocalState folder (retrievable via Device
// Portal) and mirrors every line to the debugger via OutputDebugString.
void WriteReport(const std::wstring& report);
