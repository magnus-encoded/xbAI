# xbAI — Context

## Vision
Run open-weight AI models on a retail **Xbox Series X** in Developer Mode, served
over a LAN API with a dashboard. The user is committed regardless of difficulty —
**treat constraints as engineering parameters, not feasibility gates.** "If it's
hard we will do hard things."

**Maturity staging (decided — see ADR-0003):**
- **v1 (now) — for experienced devs on dev-enabled units.** *Which* model is fixed
  at **build/config time**: a text/JSON config names the HF repo + file set; the dev
  compiles with it and pushes to their dev unit. *But the on-console flow is still
  the real flow:* the UI shows **a button for that one configured model**, the user
  presses **A** = "yes, download and run this", and it **downloads on-device →
  loads → serves → dashboards**. "Arbitrary model" = the dev may point the config at
  any model — *subject to the hard constraint that it must already be an
  **ORT-GenAI ONNX/DirectML** export* (the dev owns that; we do not convert PyTorch
  on-device).
- **Deferred to a mature end-state (not designed now):** *choosing among* models
  (in-app catalog / on-console browse), other ways to customize which model, and
  MS-Store naive-user packaging. The single-model press-A-to-download flow is v1;
  the multi-model selection layer on top of it is later. **Does not warrant design
  effort before v1 runs.**

## The hardware target
- Xbox Series X, Dev Mode = **"Universal Windows App Devkit"** (retail console in
  dev mode → **UWP-only** deployment; the Win32+GDK GameCore path needs an
  activated partner devkit we don't have).
- OS `10.0.26100.8064` (April 2026). GPU adapter `SraKmd_arden` (RDNA2),
  VendorId `1414` / DeviceId `d000`.
- **Reach it by hostname `XBOX` = `192.168.1.233`.** (`idea.md` says
  `192.158.1.233` — that is a typo; `158`→`168`.)
- **Windows Device Portal** at `https://XBOX:11443`, self-signed cert (`curl -k`),
  **no auth**; POSTs need the `CSRF-Token` cookie echoed as header `X-CSRF-Token`.
- Dev SMB share: `\\XBOX\D$` → `D:\DevelopmentFiles`, user/pw and dev account in
  your local `.env` (`SMB_USER`/`SMB_PASSWORD`), sandbox `XDKS.1`.

## Domain language
- **App profile vs Game profile** — a dev-mode UWP package classified as an *App*
  gets DX11, ~1 GB, ≤45% GPU; classified as a *Game* gets **DX12, full GPU, more
  memory, 4 exclusive + 2 shared CPU cores**. The classification is toggled
  per-app in **Dev Home** on the console (or globally via the `DefaultUWPContentTypeToGame`
  setting). **This is the single most important lever in the project.** NB: Game is
  a **resource-allocation profile, not a rendering tech** — it does not restrict the
  API surface. We classify as a Game **for the metal (GPU + memory + cores), not for
  flashy graphics**: the UI is plain **XAML** with DirectML running **headless**
  underneath on its own D3D12 device.
- **WDP** — Windows Device Portal (the `:11443` REST API). Used to deploy, launch,
  inspect, and pull files.
- **DirectML (DML)** — Microsoft's D3D12-based ML library; the native ML path on
  Xbox. The console has no CUDA/ROCm/Vulkan, so **DML/D3D12 compute is the only
  GPU compute route.**
- **WinML** — `Windows.AI.MachineLearning`, the in-box UWP wrapper over ORT+DML.
  Evaluates a **single graph forward pass** only: no autoregressive decode loop,
  KV-cache, tokenizer, or `generate()`. Proven in-sandbox (device creates), but
  serving an LLM on raw WinML means hand-rolling the whole decode loop yourself.
- **ORT-GenAI** — ONNX Runtime GenAI, the LLM decode-loop layer (KV-cache,
  sampling, `generate()`) with DirectML EP and pre-fused RoPE/MHA/MLP graphs.
  Ships as **out-of-box native DLLs** (`onnxruntime-genai.dll` + `onnxruntime.dll`
  + `DirectML.dll`); its UWP-container loadability is **unproven**. Pre-quantized
  INT4 ONNX models for it exist on Hugging Face (e.g.
  `Phi-3-mini-4k-instruct-onnx-directml-int4-awq-block-128`).
- **Path A primary = ORT-GenAI** (inherit the decode loop), with raw-WinML
  hand-rolling treated as a *disguised Path B*, not a cheap fallback — a correct
  KV-cached loop on raw WinML is ~as much work as authoring a raw DML graph.
  Choice is **provisional / empirical**: revisit if probe data favours another path.

## Current state (2026-06-04)
We built and ran a **capability probe** (`xbprobe/`, a C++/CX UWP DX12 app) on the
real console to replace desktop-doc guesses with measured facts. All three
container unknowns are now **answered**:

| Question | Measured answer |
|---|---|
| GPU compute in the UWP sandbox? | **Yes** — D3D12 device, FL 11_0, **Shader Model 6.4**, **UMA + CacheCoherentUMA** |
| DirectML usable in-sandbox? | **Yes, via WinML** — `LearningModelDevice(DirectX)` succeeds. (`LoadLibrary("DirectML.dll")` by bare name fails, err 126 — load by full path / use inbox WinML) |
| Memory ceiling? | **App ≈ 1.18 GiB → Game ≈ 4.05 GiB local budget** (shared mem 0.81→4.00 GiB). Confirmed by re-running after tagging as Game. |

See `FINDINGS.md` for the raw probe output and `docs/adr/0001-inference-backend.md`
for the backend decision.

## Decisions
- **Guiding principle:** decisions are driven by **capability and the goal, never
  developer convenience.** Cumbersome syntax or a complex dependency mix just means
  a longer road — it is *not* a reason to choose. Optimise for what extracts the
  most from the hardware.
- **Product language: C++ (LOCKED on portability grounds — ADR-0004).** Decided when
  portability became a requirement: plain-C++ **`core/`**, **C++/WinRT** for the UWP
  shell (never deprecated C++/CX), **Win32 C++** for the GDK shells. The inference core
  is already C++ (OGA C API) and GDK/GameCore is a C++-first SDK; **C# is rejected for
  the shell** because it complicates the GameCore-native port.
- **Portability-by-construction: portable `core/` + thin `pal/` (ADR-0004).** UWP gives
  only a *fraction* of the metal (~4 GiB / FL 11_0); the full machine lives in the
  **GameCore/GDK** environment, gated behind **ID@Xbox** (Business Partner Center
  account — **deliberately deferred** to the last responsible moment). To make that a
  *flip not a rewrite*: keep all platform APIs behind a small Platform Abstraction Layer
  (`pal/`: lifecycle, inbound socket, downloader, storage, UI, gamepad), with `core/`
  (inference + OpenAI server + single-flight queue) holding **zero** WinRT/GDK headers.
  Three backends over time: `pal/uwp` (now) → `pal/gdk-desktop` (**free** public PC GDK,
  validates the port on Windows with no signup) → `pal/gdk-xbox` (the gated flip).
- **Ship as a Game, not an App** (DX12 + memory). Confirmed effective.
- **Inference backend = Path A: ONNX Runtime GenAI / WinML + DirectML** (ADR-0001).
  Fallbacks if A hits a wall: B = author the transformer as a raw DML graph;
  C = custom D3D12 compute kernels (cf. Const-me/Whisper prior art).
- **North star = maximise capability**: run the **best model the hardware can
  serve**, using as much of the GPU + memory budget as we can extract. Push for the
  higher requirement; take what we get.
- **First model target = Phi-3-mini INT4 (~2 GB)**, ambition **full 4k context**
  (the higher target). Budget math against the measured **4.05 GiB** ceiling:
  weights ≈2.0 + KV @4k fp16 ≈**1.6** (Phi-3-mini is **non-GQA**: 32 layers × 32 KV
  heads × 96 head_dim) + ORT/DML/activations ≈0.3–0.5 + XAML/HTTP/baseline ≈0.2–0.3
  ⇒ **~4.1–4.4 GiB, *over* the line at full 4k**. So 4k requires **KV-cache
  quantisation** (accepted sub-task); 2k context fits comfortably (~3.3 GiB) as the
  safe fallback. Estimates above are to be **replaced by probe-measured** working
  set. **Capability lever for later models: prefer GQA** (e.g. Llama-3.2-3B /
  Qwen2.5-3B INT4) — far smaller KV ⇒ longer context and/or bigger model in the same
  budget. 8B INT4 (~4–4.5 GB weights alone) remains out of reach at this ceiling.

## Build & deploy (CLI-only; no VS IDE)
Toolchain: VS 2022 **Build Tools** (`VCTools` + `UniversalBuildTools`, `UWP.VC.v143`,
`Windows11SDK.26100`). **NB:** a stray VS "18" Build Tools install exists; its
MSBuild lacks the C++/CX UWP targets — `build.ps1` deliberately picks the install
whose tree has `Microsoft.Windows.UI.Xaml.CPP.Targets` (the 2022 one).
Flow: `./build.ps1` → `./deploy.ps1` → `./get-results.ps1`.
Signing cert: self-signed `CN=xbAI`; path + password live in `.env`
(`SIGNING_CERT_PFX` / `SIGNING_CERT_PASSWORD`). Each dev generates their own.

WDP quirks learned (baked into the scripts):
- Launch = `POST /api/taskmanager/app?appid=<base64>&package=<base64>` with `Content-Length: 0`.
- Filesystem `path` param needs a **leading backslash** (`%5CLocalState`) or the
  portal mis-concatenates and returns an empty listing.
- In-place re-deploy can wedge activation (`0x80EF0100`); uninstall+reinstall clears it.

## Open questions / next steps
1. **Does ORT-GenAI load + init inside the UWP container, and emit one token on
   the GPU?** Load **PROVEN** (T1 Run 3) and **init PROVEN** (T1c Run 6, 2026-06-05):
   the stock ORT-GenAI DirectML stack **loads AND `OgaCreateModel`-inits Phi-3-mini
   INT4 within the 4.05 GiB game budget** — weights GPU-resident at **2.01 GiB**, 2.04
   GiB free. Two caveats baked in: (a) needs the **game** profile — the App default
   (1.18 GiB) OOMs; ensure `DefaultUWPContentTypeToGame=true` / package tagged Game;
   (b) the single ~2 GB DEFAULT-heap commit is **fragmentation-sensitive** — Run 4
   OOMed at the same nominal budget on a fragmented pool, Run 6 passed after a fresh
   reboot, so **load the model early on a clean container**. **Remaining (T2): decode
   one token on the GPU, then tokens/s** (extend probe with `OgaCreateGenerator*` /
   `OgaGenerator_GenerateNextToken`). Path A is validated through init; the
   raw-WinML/DML fallback is no longer needed for load/init.
2. **Binary provenance + app-container API legality.** The container restricts not
   just the DLL search path but the **set of Win32 APIs** a native DLL may import;
   a desktop-built ORT/ORT-GenAI may fail to load or fail WACK on a forbidden
   import. Sequencing (cheap-falsification first): **(a)** package *stock release*
   `onnxruntime-genai` + `onnxruntime` + `DirectML` DLLs, load by full path, see if
   they load+init — a pass collapses the risk; **(b)** on failure, dump the import
   table / loader error to name the **specific forbidden API** (the key datum);
   **(c)** only then consider a from-source UWP/app-container build of ORT-GenAI.
   Prefer off-the-shelf; let the failure mode, not speculation, fund the build.
3. **Model acquisition.** *v1:* *which* model is fixed by **build/config**, but the
   bytes arrive via **on-device download** triggered by the press-**A** flow (the
   real UX). Dev-time SMB staging to `LocalState` stays available as a **probe/dev
   shortcut**. The on-disk layout below applies either way. Network cap declared
   (`internetClient`). Pin-downs:
   - **On-disk layout:** canonical model root = **`LocalState/models/<model-id>/`**,
     written by the downloader and read by the inference loader (same path). The
     "2 GB caveat" is really the **ONNX 2 GB protobuf limit** ⇒ model is a small
     `.onnx` graph + **external-data weight files** kept together in that folder;
     the downloader fetches a **file set**, not one blob.
   - **Probe staging (dev only):** SMB-push the model into `LocalState` via `D$`,
     *not* bundled into the appx — proves the real `LocalState` read path. `D$` is
     a **dev-mode-only** staging path; assume we replace it before ship.
   - **Degraded fallback (acceptable worst case):** download into evictable
     **`LocalCache`** and re-fetch each launch — wasteful but not terrible.
   - **Alternate end-state (banked, not the goal):** **bundle the model in the
     appx**. Loses the one-button-download flexibility and the imagined UX, but
     still serves an open model on Xbox hardware — a legitimate fallback.
4. **Push the ~4 GiB ceiling** (north-star: best model the hardware can run). Levers
   to probe: is 4.05 GiB hard or can a memory mode / `EnableGuestMta`-style budget
   raise it; **KV-cache quantisation** to afford 4k; **GQA models** to fit longer
   context / bigger weights; weight streaming. Measure real working-set first.
5. **API server** inside the game container — **decided** (see ADR-0002):
   LAN-only, cross-machine, **`StreamSocketListener` + hand-rolled HTTP/1.1**
   (pure WinRT, no native HTTP lib), **OpenAI-compatible `/v1/chat/completions`**,
   manifest adds **`privateNetworkClientServer`** (keep `internetClient` for
   downloads; no `internetClientServer` — internet exposure is the user's
   reverse-proxy problem). Loopback ban is moot: clients live on other LAN devices
   hitting `http://192.168.1.233:<port>`, never the console itself. Process shape:
   one process, three threads — **XAML UI thread** (dashboard + input) / decode loop
   (worker) / socket accept+serve (worker); requests enqueue prompts to the decode
   worker; DirectML runs headless on its own D3D12 device, off the UI thread.
   **Concurrency = single-flight**: budget fits exactly one KV cache + one GPU, so
   the server is a **FIFO-serialized single-worker** queue — concurrent HTTP
   requests are accepted but generations run one at a time; **SSE** streams the
   active job (`stream:true`); a **bounded queue returns 429/503** on overflow.
   Continuous batching is a far-future nice-to-have, not v1.
6. **Dashboard + UX** — **decided**: **XAML** for all UI, DirectML/D3D12 a
   **headless compute path** behind it, feeding XAML via data-binding. No
   hand-rolled DX12 UI ("game for the metal, not for flashy graphics"). *v1 scope:*
   a **single button** for the config-defined model; **press A** (XAML gamepad
   focus: D-pad moves, A invokes) = "download + run this" → on-device download →
   load → serve; the dashboard then shows **status (downloading/loading/serving/
   idle), the endpoint URL, and live usage (tokens/s, queue depth)**. A multi-model
   *selection* screen belongs to the mature end-state.
