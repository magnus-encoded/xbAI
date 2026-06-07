# xbAI — Empirical task board

Tasks to turn the resolved design tree (see `CONTEXT.md` + `docs/adr/`) into
measured facts. Status legend: **READY FOR AGENT** (unblocked, can start now) ·
**BLOCKED** (waiting on a dependency) · **DONE**.

> **Shared-hardware coordination:** there is **one** physical console
> (`XBOX` / 192.168.1.233). Agents may build/develop in parallel, but **serialize
> deploy + launch on the device** — only one app activated/running at a time.
> Uninstall+reinstall to clear a wedged activation (`0x80EF0100`). Record raw
> output into `FINDINGS.md`, not just conclusions.

---

## T1 — ORT-GenAI load + init gate (THE gate) — **READY FOR AGENT** · critical path
**Goal:** Answer open-question #1/#2: do *stock-release* ORT-GenAI native DLLs
**load and initialize inside the UWP app container** on the real console?
**Why first:** every downstream task (T2, the whole product) rides on this. ADR-0001
stays provisional until it passes.
**Approach (cheap-falsification, ADR-0001 / CONTEXT #2):**
1. Acquire **stock x64 release** binaries: `onnxruntime-genai.dll`, `onnxruntime.dll`,
   `DirectML.dll` (GitHub/NuGet releases — do **not** build from source yet).
2. Package all three into the `xbprobe` appx (so they sit on the app-container DLL
   search path — the earlier err-126 was absence, not prohibition).
3. In `xbprobe` (C++, reuse the harness), `LoadLibrary` each **by full path** from
   the install dir, then call the ORT-GenAI C API: create env / load config / create
   `OgaModel` against a staged model dir (see T1-stage).
4. Report per-step: loaded? (per DLL) · init/OgaModel created? · the **exact**
   `HRESULT`/`GetLastError`/loader error on any failure.
**Done when:** `FINDINGS.md` records, from the live console, whether the stack loads
+ inits, and if not, the precise failure point + error code.
**On failure → spawn T1b. On pass → unblocks T2.**

### T1-stage — stage a small INT4 model to LocalState — **READY FOR AGENT** (prereq of T1)
SMB-push **Phi-3-mini-4k INT4 ONNX/DirectML** (`.onnx` + external `.data` + tokenizer
+ `genai_config.json`) into `LocalState\models\phi3-mini-int4\` via `\\XBOX\D$`
(dev shortcut per ADR-0003 / CONTEXT #3). Verify the loader can read it by full path.

### T1b — name the forbidden API (only if T1 fails) — **BLOCKED on T1 (fail)**
Dump the import table of the failing DLL (`dumpbin /imports` or equiv) and/or capture
the loader trace; identify the **specific app-container-forbidden Win32 import**. That
error is the datum that decides whether a from-source UWP build (option c) is viable
or whether we fall back to raw-WinML/DML (Path B). Write the verdict into ADR-0001.

---

## T2 — forward pass + GPU confirmation + working-set memory — **DONE** (2026-06-05)
**Goal:** Answer open-question #1 tail + #4: decode **one token** end-to-end, confirm
it executed on the **Xbox GPU** (not CPU fallback), and measure the **real working
set** at load + during decode.
**Result (FINDINGS Run 7, probe v0.1.0.4):** PASS. A full forward pass runs on the
Series X GPU and emits **correct** output ("Paris"). **2.31 tok/s** steady-state decode;
GPU execution confirmed (dml provider + 2.50 GiB weights resident through decode + speed
far above the CPU-EP floor). Working set: 2.01 GiB weights → 2.88 GiB after 43 tokens
(~0.87 GiB KV+act / 43 tok) ⇒ **~2k context fits under 4.05 GiB, full 4k needs KV-quant**.
TTFT not measured (prefill folds into AppendTokenSequences). **ADR-0001 flipped
provisional → confirmed.** Follow-ups: time prefill/TTFT; throughput tuning
(graph-capture, FL11 op coverage); exact KV/token for product sizing.

---

## T3 — LAN inbound serving reachability probe — **DONE** (2026-06-05) · independent of T1
**Goal:** De-risk ADR-0002's load-bearing assumption: can a **Game-classified UWP app
on the Xbox accept inbound LAN TCP** reachable from the dev PC?
**Result (FINDINGS Run 8, probe v0.1.0.5):** **PASS — ADR-0002 CONFIRMED.** Declared
**`privateNetworkClientServer`** (alongside `internetClient`, no conflict with the
**Game** content-type, status 0x17), opened a WinRT **`StreamSocketListener`** on a
fixed port **8080** (`LocalPort=8080`), and served HTTP/1.1 `200 OK`. A `curl` from the
dev PC (192.168.1.214 → 192.168.1.233:8080) returned **HTTP 200** with the body in
**21 ms**; two requests served, source IP observed via `StreamSocket.RemoteAddress`.
**No friction:** no firewall rule, **no WDP/port-forwarding, no special port range** —
the app's own listener is directly LAN-reachable on the chosen port; the loopback ban
is moot because clients are on other machines. `privateNetworkClientServer` is
**necessary + sufficient** (it gates `BindServiceNameAsync`). Pure-WinRT serving path
is app-container-legal — no native HTTP lib, no forbidden-import wall. Untested
follow-ups (not blockers): SSE streaming over this socket, concurrent connections under
the single-flight queue, privileged ports (<1024).

---

## T4 — on-device Hugging Face download spike — **DONE** (2026-06-05) · independent of T1
**Goal:** Prove the v1 download path (CONTEXT #3): fetch an HF **file set** on-device
into `LocalState\models\<id>\`.
**Result (FINDINGS Run 9, probe v0.1.0.6):** **PASS.** `BackgroundDownloader`
(`internetClient` alone is sufficient) fetched the full Phi-3-mini INT4 9-file set from
HF (`microsoft/Phi-3-mini-4k-instruct-onnx`, `directml-int4-awq-block-128`) into
`LocalState\models\phi3-mini-int4-dl\`, every file at **exact expected size** (total
**2,135,846,739 B**; the 2 GB external blob `model.onnx.data` = **2,131,292,928 B**
intact). Layout = exactly what the T1c/T2 loader reads (`.onnx` + co-located external
`.data`), so download→load is a straight handoff. **Resume tested cross-session:**
killed the app mid-blob (270 MB) via WDP, relaunched, `GetCurrentDownloadsAsync` +
`AttachAsync` **resumed to completion** (smalls skipped via disk-size check) — verdict
`OK (resumed)`. Throughput ~12→23 MB/s (~100 s for 2 GB). Gotchas recorded: download
runs on a **ThreadPool worker** (blocking the main thread minutes-long trips the
activation watchdog); **BackgroundDownloader keeps running after app terminate** (blob
went 270→493 MB with no process alive) but the app **must re-attach** next launch or the
system cancels it; **`Progress.BytesReceived` on a re-attached op counts the session,
not cumulative** — use on-disk size for total/integrity; 2 GiB UWP file cap is real but
Phi-3-mini clears it (a >2 GiB single `.data` would need multi-shard external data).

---

## Product build — Wave 1: `core/` + `pal/gdk-desktop` (this dev PC, free)
All four de-risking probes (T1c/T2/T3/T4) **PASS** — the product build is unblocked.
Build it **portable-by-construction**: a plain-C++ **`core/`** (zero WinRT/GDK headers)
over a thin **`pal/`** (ADR-0004), language **C++** (locked), each backend native-built
(ADR-0005), PAL kept minimal with tolerated duplication (ADR-0006). Wave-1 target is
**`pal/gdk-desktop`** — it runs on this GPU-less Win10 box with no console and no signup
(see memory `dev-pc-gdk-desktop-harness`). The directory bones exist under `core/` and
`pal/` (see their `README.md`s).

> **Shape:** one **serial bootstrap (P0)** that resolves the chicken-and-egg knot, then a
> **parallel fan** of self-contained "here's what to do / do it" tasks that each implement
> a frozen seam or core module. The fan tasks are **BLOCKED on P0** (they need the frozen
> interface signatures) and otherwise independent of each other.

### P0 — Product bootstrap / walking skeleton (the chicken-and-egg task) — **READY FOR AGENT** · critical path · serial
**Why one big task:** the build topology, the PAL interface signatures, and a skeleton that
compiles are **mutually dependent** — you can't finalize the interfaces without a skeleton
that exercises them, can't build the skeleton without the topology, can't pick the topology
without `core/` existing as a lib. Resolve the knot in one pass; do **not** fan this out.
**Do:**
1. **Toolchain check** — install/verify the public GDK (`aka.ms/gdk` / NuGet
   `Microsoft.GDK.PC`) + Win11 SDK (22000+) on this Win10 host; confirm a
   `Gaming.Desktop.x64` build works (mirror the `build.ps1` "pick the right MSBuild" care).
2. **`core/` as a static lib** (ADR-0005) — minimal buildable lib (CMake encouraged), empty
   but linkable, zero WinRT/GDK includes.
3. **Freeze the seam signatures** in `core/pal/` — write the **minimal** interfaces forced by
   the metal (`ILifecycle`, `ISocketListener`, `IModelStore`, `IDownloader`, `IInput`,
   `IDashboard` — shrink per ADR-0006 §1) plus the core-internal contracts the fan needs to
   compose (inference-engine interface, request/response types, model-locator). These become
   **the contract**; changing a signature after P0 requires coordination.
4. **Walking skeleton** — `pal/gdk-desktop`: `WinMain` + `XGameRuntimeInitialize`, a Winsock
   `ISocketListener` that accepts a connection and returns a **canned HTTP `200`**, linking
   the `core/` lib (core may no-op). Proves topology + contract end-to-end on this box.
**Done when:** `pal/gdk-desktop` builds and runs on the dev PC, `curl` against it gets `200`;
`core/` links as a lib; the `core/pal/` interface headers are committed and declared frozen.
**Unblocks:** every Wave-1 fan task below.

### Wave-1 fan (each self-contained; **BLOCKED on P0**, then **READY FOR AGENT**)
Each codes against the frozen headers; per ADR-0006 they don't share abstractions, so they
don't collide. `core-*` are platform-agnostic; `pal-gdkdesktop-*` implement seams.
- **core-model** — model-dir layout, `genai_config.json` parse, model-id → path. *(start
  first; core-inference depends on its path contract.)* Home: `core/model/`.
- **core-inference** — OGA C-API wrapper: load from path, decode loop, yield tokens; behind
  the inference-engine interface. Validate with a **tiny** ONNX on this box (not 2 GB Phi-3).
  Home: `core/inference/`.
- **core-server** — HTTP/1.1 + OpenAI `/v1/chat/completions` + SSE over `ISocketListener`.
  Home: `core/server/`.
- **core-queue** — single-flight FIFO + lifecycle, owns the decode worker thread, 429/503 on
  overflow; wires server → inference. Home: `core/queue/`.
- **pal-gdkdesktop-socket** — promote the P0 Winsock stub to the full `ISocketListener`
  (streaming bodies for SSE, multiple connections). Home: `pal/gdk-desktop/`.
- **pal-gdkdesktop-storage** — `IModelStore` via Win32 paths (model root on disk). Home:
  `pal/gdk-desktop/`.
- **pal-gdkdesktop-downloader** — `IDownloader` (WinHTTP or XCurl/libHttpClient): resumable
  file-set fetch with progress. Home: `pal/gdk-desktop/`.
- **pal-gdkdesktop-shell** — `ILifecycle` (XGameRuntime PLM), `IInput` (GameInput press-A),
  `IDashboard` (status output — log/console first, D3D overlay later). Home: `pal/gdk-desktop/`.

### Later waves (not this batch)
- **`pal/uwp` backend** — the console v1 ship (ADR-0003): C++/WinRT + XAML mirrors of the
  seams (StreamSocketListener / BackgroundDownloader / ApplicationData / Windows.Gaming.Input,
  XAML dashboard + press-A), MSBuild/appx via `build.ps1` + signing. Implemented independently
  of gdk-desktop (ADR-0006 §3); all already de-risked by `xbprobe` (T2/T3/T4). Home: `pal/uwp/`.
- **KV-cache quantisation** — if 4k context is over the 4.05 GiB budget (CONTEXT math).
- **`pal/gdk-xbox` flip (GATED)** — full metal: ID@Xbox + GDKX + Business Partner Center, then
  `Gaming.Xbox.Scarlett.x64`, lifted memory/feature-level, optional DirectStorage. `core/`
  unchanged. The residual risk lives here, not in the PAL: does the stock ORT stack load on the
  Xbox Game OS. **Defer the signup until this step is actually started.** Home: `pal/gdk-xbox/`.
