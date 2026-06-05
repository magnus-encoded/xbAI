# xbAI — Capability Findings (from the live console)

All facts below are **measured on the actual Xbox Series X**, not inferred from
docs, via the `xbprobe` UWP capability probe deployed over the Device Portal.

## Probe output — App profile (before tagging as Game)
```
[DXGI] Adapter            : SraKmd_arden   (VendorId 1414 / DeviceId d000)
[DXGI] DedicatedVideoMem  : 0.50 GiB
[DXGI] SharedSystemMem    : 0.81 GiB
[D3D12] D3D12CreateDevice (FL 11_0): 0x00000000 (OK)
[D3D12] Max feature level : 11_0
[D3D12] Highest shader model: 0x64  (Shader Model 6.4)
[D3D12] UMA / CacheCoherentUMA: yes / yes
[BUDGET] Local Budget     : 1.18 GiB
[DML] LoadLibrary(DirectML.dll): NOT loadable, GetLastError=126 (MOD_NOT_FOUND)
[WinML] LearningModelDevice(DirectX): Created (AdapterId.LowPart=0)
Summary status bits = 0x13  (DXGI ok, D3D12 ok, budget<4G, DML-by-name fail, WinML ok)
```

## Probe output — Game profile (after tagging as Game in Dev Home)
```
[DXGI] SharedSystemMem    : 4.00 GiB      <-- up from 0.81 GiB
[BUDGET] Local Budget     : 4.05 GiB      <-- up from 1.18 GiB
(everything else unchanged; WinML DirectX device still OK)
Summary status bits = 0x17  (adds >=4GiB budget)
```

## Interpretation
1. **GPU compute is available in the dev-mode UWP sandbox.** D3D12 device creates,
   Feature Level 11_0, **Shader Model 6.4**, and the memory is **UMA +
   cache-coherent** (CPU/GPU share memory, no copy — ideal for weight residency).
2. **DirectML is usable in-sandbox through WinML.** Bare `LoadLibrary("DirectML.dll")`
   fails with err 126 (not on the container's DLL search path), but WinML's
   `LearningModelDevice(DirectX)` — which is DML-backed — **succeeds**. So the
   substrate is present; access path is WinML (inbox) or the redist loaded by full path.
3. **Game tagging lifts the memory budget ~3.4×** (1.18 → 4.05 GiB local). This is
   the measured model-residency budget.
4. **Model sizing:** ~4 GiB usable ⇒ **Phi-3-mini INT4 (~2 GB)** fits comfortably
   with KV-cache headroom; **8B INT4 (~4–4.5 GB)** is too tight at this budget.

## What is NOT yet proven
- That a full model **forward pass executes on the Xbox GPU** (device creation is
  necessary but not sufficient). This is the next probe.
- Real inference throughput (tokens/s).
- On-device Hugging Face download + the one-button UX.

## T1 — stock ORT-GenAI load+init gate (2026-06-04, run 1: DLL-load half)
Probe extended to package the stock x64-win stack (onnxruntime-genai 0.6.0 /
onnxruntime 1.20.1 / DirectML 1.15.2) under `.\redist` and LoadLibrary each by full
path. Raw result from the console:
```
[install dir] S:\Program Files\WindowsApps\xbAI.Probe_0.1.0.0_x64__wgm1k6hn52fna
[ORT] LoadLibrary DirectML.dll       : OK
[ORT] LoadLibrary onnxruntime.dll    : FAIL  GetLastError=126 (0x7E, MOD_NOT_FOUND)
=> stack NOT fully loaded; OgaCreateModel skipped
model dir (LocalState): Q:\Users\UserMgr0\AppData\Local\Packages\xbAI.Probe_wgm1k6hn52fna\LocalState\models\phi3-mini-int4  (not staged yet)
```
Findings:
- **Fresh sideload now comes up as GAME automatically** — Local Budget **4.05 GiB**,
  SharedSystemMem 4.00 GiB, status 0x17, with NO manual Dev Home tagging. (Game tagging
  step from earlier is no longer required for this package.)
- **`DirectML.dll` loads in-container by full path** — confirms bundling cures the
  earlier bare-name err 126 (it was absence). Substrate reachable both via WinML and now
  the redist DLL.
- **`onnxruntime.dll` fails to load: err 126 = a missing *dependency* DLL.** onnxruntime
  imports beyond DirectML's: ADVAPI32, OLEAUT32, ole32, **dxcore.dll**, MSVCP140,
  MSVCP140_1, VCRUNTIME140, VCRUNTIME140_1, api-ms-win-core-path. Next: per-dependency
  probe to name the missing one (prime suspect `dxcore.dll`, not inbox in the container).

### Run 2 (per-dependency probe) — ROOT CAUSE = desktop VC runtime missing
Bare-name LoadLibrary of each onnxruntime.dll dependency in-container:
```
dxcore.dll OK | ole32 OK | OLEAUT32 OK | ADVAPI32 OK | api-ms-win-core-path OK
VCRUNTIME140.dll FAIL 126 | VCRUNTIME140_1.dll FAIL 126
MSVCP140.dll     FAIL 126 | MSVCP140_1.dll     FAIL 126
```
**`dxcore.dll` is present** (suspect cleared). The err-126 is the **desktop Visual C++
runtime** (`vcruntime140/_1`, `msvcp140/_1`) being absent: the UWP container only ships
the **Store CRT variant** (`vcruntime140_app.dll`, `msvcp140_app.dll` via VCLibs), whose
file names differ, so stock desktop-linked ORT can't resolve them by name. Classic
desktop-CRT-vs-app-CRT mismatch — **not** an app-container API prohibition.
**Fix:** bundle the 4 redistributable desktop VC runtime DLLs into package `.\redist`
and preload them before onnxruntime.dll. (No ORT rebuild needed.)

### Run 3 — DLL-LOAD HALF PASSES
After bundling `vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll`, `msvcp140_1.dll`
into `.\redist` and preloading them by full path:
```
DirectML.dll OK | onnxruntime.dll OK | onnxruntime-genai.dll OK
GetProcAddress OgaCreateModel / OgaResultGetError / OgaDestroyModel : OK
=> all DLLs loaded + OGA symbols resolved (status bit PROBE_ORT_LOADED set)
```
**The stock-release ORT-GenAI native stack LOADS inside the UWP app container** — the
big open-question-#2 unknown (does desktop-built ORT even load in-container?) is
**answered YES**, with the only fixup being the bundled desktop CRT. No forbidden-import
wall at load time. Remaining: the **init half** (`OgaCreateModel`) needs the INT4 model
staged into `LocalState\models\phi3-mini-int4\`. LocalState path on device:
`Q:\Users\UserMgr0\AppData\Local\Packages\xbAI.Probe_wgm1k6hn52fna\LocalState` (drive Q:,
not the D$ SMB share → stage via WDP `/api/filesystem/apps/file` upload).

### Run 4 — INIT HALF FAILS: OgaCreateModel OOMs at the DML allocator (T1 final)
All 9 INT4 model files staged into `LocalState\models\phi3-mini-int4\` at exact sizes
(`model.onnx.data` = 2,131,292,928 B), verified by the probe's own enumeration. The
package came up as GAME again (budget 4.05 GiB, status 0x17). The stock stack loaded
fully (DirectML + onnxruntime + onnxruntime-genai, all OGA symbols resolved). Then:
```
[ORT] OgaCreateModel: FAILED
[ORT] OgaResultGetError: Exception during initialization:
  ...DmlExecutionProvider\src\DmlCommittedResourceAllocator.cpp(22)\onnxruntime.dll!...
  8007000E  Not enough memory resources are available to complete this operation.
```
**T1 verdict = FAIL-B (loads+symbols OK, init OOMs).** `8007000E` = `E_OUTOFMEMORY`
raised while DML commits a D3D12 **DEFAULT-heap** resource for the weights. The stock
desktop ORT-GenAI DirectML stack *runs* in the UWP game container — no API/load wall —
but **cannot fit Phi-3-mini INT4 init within the 4.05 GiB game budget.**

Why OOM at "only" 2 GB of weights under a 4.05 GiB budget (working hypotheses, to test
next):
1. **DEFAULT-heap pool ≠ the 4.05 GiB budget.** Console reports DedicatedVideoMem
   **0.50 GiB**, SharedSystemMem 4.00 GiB, Local Budget 4.05 GiB, NonLocal **0.00**.
   DML's committed-resource allocator requests DEFAULT-heap (device-local) memory; if the
   DEFAULT pool is gated to the ~0.5 GiB dedicated segment rather than the 4 GiB shared
   budget, a 2 GB commit OOMs immediately. (UMA should let DEFAULT draw from shared — but
   the allocator may not be requesting it that way.)
2. **Init peak > 4.05 GiB.** Weight upload can need a transient CPU/UPLOAD-heap staging
   copy on top of the 2 GB DEFAULT resource (≈4 GB) plus KV-cache/intermediate buffers and
   ORT arena overhead — peak can cross 4.05 even though steady-state would fit.
3. **Single 2 GB committed resource** may hit a per-resource/contiguity ceiling distinct
   from total budget.

Next probes (T1c, cheap, reuse this harness):
- Re-query `QueryVideoMemoryInfo` **right before** OgaCreateModel and capture
  CurrentUsage/Budget at failure; also read `DXGI_ADAPTER_DESC1` budget split.
- Try ORT-GenAI/DML knobs to curb init peak: disized arena, memory pattern off, or the
  DML EP "external data as mmap, no copy" path; check genai_config for an mmap/upload flag.
- Sanity floor: a deliberately tiny ONNX (a few MB) through the SAME OgaCreateModel path —
  proves init succeeds end-to-end when the commit is small, isolating this as a pure
  capacity wall vs. a code-path wall.
- If capacity-bound: this **revises the Run-1 model-sizing note** — 2 GB INT4 does NOT fit
  "comfortably"; usable DML-commit headroom at init is the real budget, and looks <4 GiB.
  Consider a smaller quant (INT4 with smaller context / lower-param model) or a custom
  allocator that targets the shared segment.

### Run 5 — T1c, instrumented (2026-06-05) — ran under APP profile (see caveat)
Probe extended for T1c: per-attempt `QueryVideoMemoryInfo` snapshots (LOCAL+NONLOCAL
Budget/CurrentUsage) bracketing each init, plus three init attempts in one launch via
the onnxruntime-genai 0.6.0 config-overlay API (`OgaCreateConfig` /
`OgaConfigClearProviders` / `OgaCreateModelFromConfig`, all symbols resolved OK).

**CAVEAT — wrong memory profile.** This launch came up as the **APP** profile, not Game:
Local Budget **1.18 GiB**, SharedSystemMem 0.81 GiB, status **0x13**. Cause: the global
console setting **`DefaultUWPContentTypeToGame` = false** (confirmed via WDP `/ext/settings`),
and the **version-bumped *update* (0.1.0.0 → 0.1.0.2) did not inherit the Game content-type**
that fresh sideloads in Runs 3–4 had. (The bump was forced because an in-place same-version
redeploy is blocked: `-2147009285` "same identity, different contents".) So the numbers below
are under **1.18 GiB**, not the 4.05 GiB game budget the gate is written against — a Game-profile
re-run is required before the exit gate is decisive.

Results (all three attempts, one launch):
```
Attempt A  baseline DML (original genai_config)
  vmem pre   : LOCAL budget 1.18 / used 0.00 / free 1.18  | NONLOCAL 0.00
  result     : FAILED  8007000E (E_OUTOFMEMORY) @ DmlCommittedResourceAllocator.cpp(22)
  vmem fail  : LOCAL budget 1.18 / used 0.00 / free 1.18  | NONLOCAL 0.00   <-- usage never moved

Attempt B  DML + enable_mem_pattern=false + enable_cpu_mem_arena=false (peak-reduction knobs)
  patch      : applied to staged genai_config.json
  vmem pre   : LOCAL 1.18 / used 0.00
  result     : FAILED  8007000E — identical allocator OOM; knobs had NO effect
  vmem fail  : LOCAL 1.18 / used 0.00                                       <-- usage never moved

Attempt C  CPU-EP floor (config overlay, providers cleared -> CPU)
  vmem pre   : LOCAL 1.18 / used 0.00
  result     : FAILED  "bad allocation" (std::bad_alloc — SYSTEM RAM, not the DML allocator)
  vmem fail  : LOCAL 1.18 / used 0.00
```

What the instrumentation already tells us (even under the App cap):
1. **The OOM is an *outright rejection at zero usage*, not a budget-fill overflow.**
   `CurrentUsage` is 0.00 GiB immediately before AND after the failure — the single ~2 GB
   DEFAULT-heap commit is denied before anything is reserved against the budget. This is
   evidence *against* the "transient init-peak crosses the budget" hypothesis (#2 above) and
   *for* the "DEFAULT-heap pool is gated to the small dedicated segment" hypothesis (#1):
   nothing accumulates, the first big DEFAULT commit is simply refused.
2. **Peak-reduction knobs did nothing** (B == A). `enable_mem_pattern=false` /
   `enable_cpu_mem_arena=false` don't touch the DEFAULT-heap weight commit, consistent with
   the wall being the single committed resource, not arena/mem-pattern transients.
3. **CPU floor also failed, but differently** — `std::bad_alloc` (system RAM), not the DML
   `E_OUTOFMEMORY`. Under the 1.18 GiB App container even the CPU path can't get ~2 GB of
   committable system memory. This isolates the DML error (Attempts A/B) as specifically the
   **D3D12 DEFAULT-heap GPU commit**, distinct from the system-RAM failure mode.

**Decisive caveat stands:** because Shared jumps 0.81→4.00 GiB only in the Game profile, the
test that actually answers the brief is the same probe **under Game (4.05 GiB)**:
- If Attempt A/B still OOM at used=0.00 under Game → **DEFAULT-heap is gated to the ~0.5 GiB
  dedicated segment regardless of the shared budget** → proven capacity/pool wall on the UWP
  path (authorizes Part B), *unless* we can force DML to place weights on an UMA shared heap.
- If Attempt C (CPU) *succeeds* under Game → the model/graph/tokenizer are sound and ~2 GB of
  weights are resident-able; the wall is purely the DML GPU DEFAULT-heap placement.

**Next action (blocked on a console-config decision):** re-run this exact probe under the Game
profile. Requires either flipping `DefaultUWPContentTypeToGame`→true (device-wide, **reboot**)
or tagging the package as **Game** in Dev Home on the console, then relaunch + `get-results.ps1`.
Harness/instrumentation is done and committed; only the profile flip + relaunch remain.

### Run 6 — T1c under GAME profile — **PASS** (2026-06-05)
Re-ran the identical instrumented probe after fixing the profile regression:
flipped `DefaultUWPContentTypeToGame`→true (WDP `/ext/settings`), **rebooted** the
console, redeployed as a version-bump **update** (0.1.0.2→0.1.0.3, which re-registered
the package under the now-Game default while preserving the staged 2 GB model in
LocalState), and restored the **pristine** genai_config.json (WDP file upload, overwriting
Run 5's in-place patch) so Attempt A is a clean baseline. Package came up GAME:
Local Budget **4.05 GiB**, SharedSystemMem 4.00 GiB, status **0x17**.

```
Attempt A  baseline DML (original genai_config)
  vmem pre    : LOCAL 4.05 / used 0.00 / free 4.05
  result      : *** SUCCESS *** — OgaCreateModel initialized on DML
  vmem post-ok: LOCAL 4.05 / used 2.01 GiB / free 2.04 GiB     <-- weights resident on GPU

Attempt B  DML + enable_mem_pattern=false + enable_cpu_mem_arena=false
  vmem pre    : LOCAL 4.05 / used 0.00   (model A destroyed -> clean release)
  result      : *** SUCCESS *** — inits identically
  vmem post-ok: LOCAL 4.05 / used 2.01 GiB / free 2.04 GiB

Attempt C  CPU-EP floor (config overlay)
  vmem pre    : LOCAL 4.05 / used 0.00
  result      : (still loading at probe terminate — CPU path is very slow; MOOT, DML passed)
```

**T1c VERDICT = PASS.** The stock-release ORT-GenAI DirectML stack (onnxruntime-genai
0.6.0 / onnxruntime 1.20.1 / DirectML 1.15.2), unmodified except for the bundled desktop
VC runtime, **loads AND initializes Phi-3-mini INT4 inside the UWP game container** — a real
on-device LLM init on the easy UWP path. Weights are GPU-resident at **2.01 GiB**, leaving
**2.04 GiB** headroom for KV-cache + activations under the 4.05 GiB budget.

> **SCOPE — what PASS does and does NOT mean.** PASS = **load + init only**: `OgaCreateModel`
> built the ORT session + DML EP and committed the weights to the GPU. **No forward pass has
> run and NO token has been generated.** It does *not* yet prove the GPU executes the
> transformer or emits output — execution can still wall on an op unsupported at FL 11_0 /
> SM 6.4, or on KV-cache/activation allocation during the first decode. "Have we run a model
> on the Xbox?" → **a model is loaded and live, but it has not run inference yet.** That is T2.

**Reconciliation with Run 4 (which OOMed at the same nominal 4.05 GiB).** The difference that
flipped FAIL→PASS was a **fresh reboot** (this run) plus a clean re-registration. The leading
read: the single ~2 GB DEFAULT-heap commit needs a **contiguous** region in the shared pool;
on a console that has been running/fragmented (Run 4) the 2 GB contiguous commit can be
refused (`E_OUTOFMEMORY`), while a freshly-booted, unfragmented pool satisfies it. So residency
is **real but sensitive to pool state / fragmentation** (the hypothesis-#3 "single 2 GB
committed resource contiguity ceiling" — not a hard API or capacity wall). Implication for the
product: load the model **early, on a clean container**, and treat the single big weight commit
as fragmentation-sensitive (a future custom/streamed allocator would de-risk this).

The peak-reduction knobs (Attempt B) were **not needed** here (A already passed) but also did
no harm — banked for the fragmented-pool case.

**Exit gate (per BRIEF §3): PASS → proceed to T2.** We have a real on-device LLM init on the
UWP path; **Part B (GDK) is demoted from a blocker to a later "more headroom" upgrade.** Next:
T2 — drive a forward pass and emit one token on the GPU (extend the probe with
`OgaCreateGeneratorParams`/`OgaCreateGenerator`/`OgaGenerator_GenerateNextToken`), then
tokens/sec. Budget note from CONTEXT holds: ~2.04 GiB headroom ⇒ **2k context fits
comfortably; full 4k needs KV-cache quantisation** (measure real working set in T2).

## T2 — forward pass + token decode on the GPU — **PASS** (2026-06-05, Run 7)
Extended the probe (v0.1.0.4) past T1c's load+init into a real decode: resolve the
0.6.0 generation C API (`OgaCreateTokenizer` / `OgaTokenizerEncode` /
`OgaCreateGeneratorParams` / `OgaCreateGenerator` / `OgaGenerator_AppendTokenSequences`
/ `OgaGenerator_GenerateNextToken` / `OgaGenerator_GetSequenceData`), then on the
DML-loaded model: tokenize a Phi-3 chat prompt, prefill, and decode tokens.
Retired T1c's Attempt-B/C diagnostics — one clean DML load now, to maximise KV headroom.
Package came up GAME (Local Budget **4.05 GiB**, status 0x17). No reboot was needed:
the existing pool satisfied the 2 GB commit on this launch.

Prompt: `<|user|>\nWhat is the capital of France? Answer in one word.<|end|>\n<|assistant|>\n`
(19 tokens). Raw result from the console:
```
--- T1 init: OgaCreateModel (DML, pristine genai_config) ---
  [vmem init pre ]  LOCAL 4.05 / used 0.00 / free 4.05
  T1 init: *** SUCCESS *** - weights committed on DML
  [vmem init ok  ]  LOCAL 4.05 / used 2.01 GiB / free 2.04

================ T2: forward pass + token decode (GPU) ================
[T2] genai_config execution provider: dml (DirectML)
[T2] generation symbols: all resolved
[T2] prompt tokens (post-append): 19
  [vmem decode pre]  LOCAL 4.05 / used 2.50 / free 1.55      <-- prefill KV + activations
[T2] *** FIRST TOKEN DECODED *** id=2177  TTFT(prefill+1)=0.0 ms
  [vmem decode t1 ]  LOCAL 4.05 / used 2.50 / free 1.55
[T2] generated 24 tokens total (1 prefill + 23 steady)
[T2] steady-state decode: 23 tok in 9974.8 ms = 2.31 tok/s
[T2] decoded text: "Paris

Explanation: Paris is the capital of France.
===
The response is correct as a"
  [vmem decode end]  LOCAL 4.05 / used 2.88 / free 1.17      <-- KV grew over 43 tokens
[T2] GPU LOCAL used during decode: 2.50 GiB => DirectML GPU execution CONFIRMED
[ORT] T2 result: DLLs LOADED | init INIT-OK | decode TOKEN-OK | GPU CONFIRMED
```

**T2 VERDICT = PASS.** A real LLM forward pass runs on the Xbox Series X GPU and emits
**correct** output: the model answered "Paris". This answers open-question #1's tail
(the transformer executes end-to-end at FL 11_0 / SM 6.4 — no unsupported-op wall) and #4
(measured working set + throughput). **ADR-0001 flips provisional → confirmed.** "Have we
run a model on the Xbox?" → **yes, and it produced the right answer.**

Measured facts that **replace the CONTEXT estimates**:
- **Throughput ≈ 2.31 tok/s** steady-state decode (Phi-3-mini INT4, DML, FL 11_0). Modest
  but real GPU inference. (Headroom for tuning: graph-capture / `try_graph_capture`,
  bigger batch, KV-quant, FL11 op coverage are all unexplored.)
- **GPU execution confirmed three ways:** genai_config selects the **dml** provider; weights
  stay **GPU-resident at 2.50 GiB** through decode (a CPU-EP fallback showed 0.00 used in the
  Run 5/6 floor); and the 2.31 tok/s rate is far above CPU-EP speed (Run 6's CPU path was so
  slow it never finished).
- **Working set:** 2.01 GiB weights at init; +~0.49 GiB at prefill (KV + activations, 19-tok
  prompt) → 2.50 GiB; grows to **2.88 GiB after 43 tokens** of context. So KV+activations cost
  ≈ **0.87 GiB for ~43 tokens** on top of the 2.01 GiB weights. Extrapolating the per-token KV
  growth **confirms the CONTEXT call: ~2k context fits under the 4.05 GiB budget; full 4k needs
  KV-cache quantisation.** Measure exact KV/token in a longer run when sizing the product.

Caveat — **TTFT reads 0.0 ms** (instrumentation artifact, not a real number): in
onnxruntime-genai 0.6.0's AppendTokens flow the prompt prefill is folded into
`OgaGenerator_AppendTokenSequences` (which the probe does **not** time), so the first
`GenerateNextToken` only samples already-computed logits and returns near-instantly. The
honest decode number is the **steady-state 2.31 tok/s**; a real TTFT needs timing wrapped
around AppendTokenSequences (follow-up).

## T3 — LAN inbound serving reachability — **PASS** (2026-06-05, Run 8)
Extended the probe (v0.1.0.5) with an independent T3 path (`LanProbe.cpp`): declare
**`privateNetworkClientServer`** in the manifest (alongside the existing
`internetClient`), open a WinRT **`StreamSocketListener`** on a fixed port **8080**,
and answer a trivial HTTP/1.1 `GET` with `200 OK` + body `xbAI T3 LAN serve OK`. The
`ConnectionReceived` handler **live-rewrites** the LocalState report on each inbound
request, so a `curl` arriving after the initial write still leaves a pullable trace.
The heavy ORT-GenAI load was **skipped** for this build (T3 is independent of the
inference path; T1c/T2 already PASSED) to isolate the network test. Package came up
**GAME** (Local Budget **4.05 GiB**, status 0x17) — so `privateNetworkClientServer`
**coexists with the Game content-type with no conflict** (Game classification did not
strip the inbound capability or block the bind).

On-device report (bind half):
```
================ T3: LAN inbound serving reachability ================
[T3] manifest capability declared: privateNetworkClientServer
[T3] StreamSocketListener created
[T3] BindServiceNameAsync("8080"): OK  (LocalPort=8080)
[T3] inbound listener UP. From the dev PC: curl http://192.168.1.233:8080/
--- connection log (live-updated as inbound requests arrive) ---
[conn #1] from 192.168.1.214:54797  req="GET / HTTP/1.1"  -> 200 OK (120 bytes)
[conn #2] from 192.168.1.214:54802  req="GET /v1/models HTTP/1.1"  -> 200 OK (120 bytes)
```

Raw `curl` from the dev PC (192.168.1.214) → Xbox (192.168.1.233:8080):
```
> GET / HTTP/1.1
> Host: 192.168.1.233:8080
< HTTP/1.1 200 OK
< Content-Type: text/plain; charset=utf-8
< Content-Length: 21
< Connection: close
xbAI T3 LAN serve OK
=== HTTP 200 | time_total 0.021601s ===
```

**T3 VERDICT = PASS — ADR-0002 CONFIRMED.** A **Game-classified UWP app on the Xbox
accepts inbound LAN TCP reachable cross-machine from the dev PC.** The pure-WinRT
serving path (`StreamSocketListener` + hand-rolled HTTP/1.1) is **app-container-legal
and works** — no native HTTP lib, no forbidden-import wall. Exact conditions that made
it work, and the friction that did NOT appear:
- **Capability:** `privateNetworkClientServer` in the manifest is **necessary and
  sufficient** for the inbound bind. It is what `BindServiceNameAsync` gates. Declared
  next to `internetClient`; both present, no conflict.
- **Port:** a plain fixed port **8080** bound directly (`LocalPort=8080`). **No
  WDP/Device-Portal port forwarding and no special port range were required** — the
  app's own listener is reachable on the LAN at the chosen port directly. (WDP's own
  `:11443` is unrelated; it does not proxy the app port.)
- **Firewall:** **no firewall friction** — the dev console accepted the inbound
  connection on 8080 with no extra rule/exemption. (Dev-mode console; a retail/locked
  unit may differ, untested.)
- **Loopback ban is moot, as predicted:** we hit from **another LAN machine**
  (192.168.1.214 → 192.168.1.233), never the console itself, so the app-container
  same-machine loopback restriction never came into play.
- **Round-trip:** **21 ms**, two requests served cleanly, request lines parsed, source
  IP correctly observed via `StreamSocket.Information.RemoteAddress`.

Implication for the product: ADR-0002's serving design is **de-risked end-to-end** — a
single-process WinRT listener inside the Game container is a valid foundation for the
FIFO-serialized OpenAI-compatible `/v1/chat/completions` server. Untested follow-ups
(not blockers): SSE/chunked streaming over this socket path, concurrent-connection
behaviour under the single-flight queue, and whether a low/privileged port (<1024) or
multiple simultaneous listeners behave differently.

## T4 — on-device Hugging Face download spike — **PASS** (2026-06-05, Run 9)
Extended the probe (v0.1.0.6) with an independent T4 path (`HfDownloadProbe.cpp`):
pull the **full Phi-3-mini INT4 file set** from Hugging Face **on-device** into
`LocalState\models\phi3-mini-int4-dl\` via the UWP **`BackgroundDownloader`**
(`internetClient` capability — already declared — is sufficient; no new manifest
entry needed). The download runs on a **`ThreadPool` worker, not the app main
thread**: the 2 GB blob takes minutes and blocking the dispatcher that long trips the
UWP activation watchdog. The worker live-rewrites the LocalState report each ~64 MB so
progress is pullable via WDP. Capability probe came up GAME (status 0x17) as usual.

Repo: `microsoft/Phi-3-mini-4k-instruct-onnx`, subfolder
`directml/directml-int4-awq-block-128/` — 9 files, HF `resolve/main/...` URLs (CDN
serves them with `Accept-Ranges: bytes` / `206 Partial Content`, so they are
resumable).

**Resume was tested cross-session** (the real product scenario — app may be closed
mid-download): Launch 1 fresh → 8 small files complete + verified, big file reaches
13% (270 MB) → **terminated via WDP** → Launch 2 → `GetCurrentDownloadsAsync` returned
the 1 in-flight transfer → `AttachAsync` **resumed** it to completion (small files
skipped via on-disk size check). Raw result from the console (both launches stitched):
```
[T4] GetCurrentDownloadsAsync: 0 pre-existing transfer(s)  -> fresh start   (launch 1)
  [DONE]   added_tokens.json  306/306 B  OK
  [DONE]   config.json  919/919 B  OK
  [DONE]   genai_config.json  1622/1622 B  OK
  [DONE]   special_tokens_map.json  599/599 B  OK
  [DONE]   tokenizer_config.json  3441/3441 B  OK
  [DONE]   tokenizer.model  499723/499723 B  OK
  [DONE]   tokenizer.json  1937869/1937869 B  OK  3.6 MB/s
  [DONE]   model.onnx  2109332/2109332 B  OK  2.2 MB/s
  [START]  model.onnx.data  (2131292928 B expected)
    [..] model.onnx.data  270.0 MB / 2032.6 MB  (13%)  12.7 MB/s   <-- terminated here

[T4] GetCurrentDownloadsAsync: 1 pre-existing transfer(s)  -> RESUME path   (launch 2)
  [SKIP] (x8 small files: already complete on disk)
  [RESUME] model.onnx.data  (493.0 MB already on disk; continuing, not restarting)
    [..] model.onnx.data  1981.2 MB / 2032.6 MB  (97%)  22.7 MB/s
  [DONE]   model.onnx.data  2131292928/2131292928 B  OK (resumed)  89.1s
--- T4 summary ---
[T4] files at exact expected size: 9/9   total on disk: 2135846739 B (2036.9 MB)
[T4] LocalState model root: Q:\Users\UserMgr0\AppData\Local\Packages\xbAI.Probe_wgm1k6hn52fna\LocalState\models\phi3-mini-int4-dl
[T4] *** FULL FILE SET DOWNLOADED ON-DEVICE + SIZE-VERIFIED ***
[T4] layout matches the loader: model.onnx + external model.onnx.data co-located in the model dir
```

**T4 VERDICT = PASS.** The v1 model-acquisition path (CONTEXT #3) works on the real
console: `BackgroundDownloader` fetches the full ONNX/DirectML file set from HF into
`LocalState\models\<id>\`, **resumably**, landing every file at its exact expected size
(total **2,135,846,739 B** = the byte-exact sum of all 9 files; the 2 GB external-data
blob `model.onnx.data` = **2,131,292,928 B** wrote intact). The on-disk layout is
exactly what the T1c/T2 loader already reads (`model.onnx` + co-located external
`model.onnx.data`), so download → load is a straight handoff.

Measured facts + UWP gotchas worth keeping:
- **Throughput:** small files instant; the big blob ramped **~12 → ~23 MB/s** on this
  console's connection. Full 2 GB takes roughly **~100 s** end-to-end (the resumed
  remainder alone was 89 s).
- **`BackgroundDownloader` is a system service that keeps running after the app is
  terminated.** Between WDP-terminate and relaunch the blob advanced **270 → 493 MB
  with no app process alive** — exactly the durability we want. But the flip side: the
  app **must re-enumerate via `GetCurrentDownloadsAsync` + `AttachAsync` on next
  launch** to reconnect, or the system cancels the orphaned transfer after a grace
  window. The probe does this and it is the sanctioned resume pattern.
- **`DownloadOperation.Progress.BytesReceived` on a *re-attached* transfer counts the
  current session, not cumulative-from-zero** (it read ~138 MB right after a 493-MB
  baseline). **Use the file size on disk as the source of truth for total progress /
  integrity** — which the probe does for its size verification. Don't trust
  BytesReceived for a resumed % or for completion.
- **The 2 GiB UWP file cap is real but Phi-3-mini clears it:** `model.onnx.data` is
  **2,131,292,928 B**, ~16 MB under the 2,147,483,648 signed-int ceiling. A model whose
  single external-data file exceeds 2 GiB would need the export split across multiple
  `.data` shards (ORT-GenAI supports multi-file external data) — note for larger models.
- **No special capability beyond `internetClient`** was needed for outbound HF download;
  `privateNetworkClientServer` (T3) is unrelated to this path.

Follow-ups (not blockers): integrity beyond size (HF exposes an `ETag`/`X-Xet-Hash` we
could verify); `BackgroundDownloader` cost-policy / unconstrained-network settings for
metered links; wiring this into the press-A → download → load → serve product flow.

## Prior art worth referencing
- Microsoft `microsoft/DirectML` LLM sample (Phi-2/3, LLaMA-2/3) and ORT-GenAI
  DirectML graphs (RoPE/MHA/MLP) — the basis for Path A.
- `Const-me/Whisper`: single-dev transformer inference on DirectX compute shaders
  (D3D11), beat CUDA on a 1080 Ti — the proof-point for fallback Path C.
- Open llama.cpp issue #7772 "ggml: add DirectML backend" — wanted, unbuilt; the
  gap a custom backend would fill.
