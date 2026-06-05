# T1 — ORT-GenAI load+init gate — pre-deploy working notes

> **Status: PLANNING / pre-first-deploy.** Nothing here is measured on the console
> yet — these are acquisition + strategy notes so the work survives a pause/compaction.
> Measured results go in `FINDINGS.md` (kept pure: console-measured only).
> Task definition: `TASKS.md` → T1 / T1-stage.

## Decision: which binaries to gate on

Use a **single ABI-matched, low-confound set** — DirectML EP statically built into
`onnxruntime.dll`, with an explicitly pinned DML redist. This is the 0.5/0.6 line, the
last before ORT-GenAI's EP-as-plugin pivot. Squarely in the Phi-3-mini DirectML support
window. **Chosen set:**

| Package (NuGet) | Version | Gives us |
|---|---|---|
| `Microsoft.ML.OnnxRuntimeGenAI.DirectML` | **0.6.0** | `onnxruntime-genai.dll` |
| `Microsoft.ML.OnnxRuntime.DirectML` | **1.20.1** | `onnxruntime.dll` (DML EP built in) |
| `Microsoft.AI.DirectML` | **1.15.2** | `DirectML.dll` |

Rationale: T1 is a *falsification gate* — I want the config least likely to let an
incidental confound (new EP-plugin plumbing, config-schema drift) masquerade as the real
answer ("does the stock stack load+init in the UWP container?"). Latest (genai 0.14 /
ORT 1.23) bundles DML and uses the newer EP-selection path → more variables.
**Deferred:** once 0.6.0 passes the gate, re-verify a bump to latest in the product.

Dependency pinning verified from nuspecs: genai 0.6.0 → ORT.DirectML **1.20.1** +
Microsoft.AI.DirectML **1.15.2** (explicit). From genai 0.8.3+ the explicit DirectML dep
disappears (bundled) and ORT jumps to 1.22+/1.23.

### Acquired (DONE)
nupkgs downloaded to `xbprobe/thirdparty/nupkg/` (NOT yet extracted):
- `genai.zip`    1,500,323 B  (genai 0.6.0)
- `ortcore.zip`  15,844,459 B (ORT.DirectML 1.20.1)
- `directml.zip` 202,291,463 B (Microsoft.AI.DirectML 1.15.2 — large: all arches + debug layer; extract x64 `DirectML.dll` only)

### Still TODO on binaries
- Extract x64 native DLLs from the three nupkgs (`runtimes/win-x64/native/`).
- `dumpbin /exports onnxruntime-genai.dll` → lock exact OGA symbol names before coding.
- `dumpbin /imports` on each → eyeball for app-container-forbidden Win32 imports
  (head-start on T1b; confidence read on whether load will even succeed).
- Confirm VC-runtime dependency is satisfied by the already-deployed `Microsoft.VCLibs.x64.14.00`
  framework pkg (ORT 1.20.1 built w/ VS2022 → vcruntime140/_1, msvcp140). Package the
  loose vcruntime DLLs only if a load fails with err 126.

## Model file set (T1-stage)

HF repo `microsoft/Phi-3-mini-4k-instruct-onnx`, subdir
`directml/directml-int4-awq-block-128/`. Resolve URL pattern:
`https://huggingface.co/microsoft/Phi-3-mini-4k-instruct-onnx/resolve/main/directml/directml-int4-awq-block-128/<file>`

| File | Bytes | Needed for load+init? |
|---|---|---|
| `genai_config.json` | 1,622 | **yes** (the dir arg to OgaCreateModel) |
| `model.onnx` | 2,109,332 | **yes** (graph) |
| `model.onnx.data` | **2,131,292,928** | **yes** (external weights) |
| `tokenizer.json` | 1,937,869 | **yes** |
| `tokenizer.model` | 499,723 | yes (tokenizer) |
| `tokenizer_config.json` | 3,441 | yes |
| `special_tokens_map.json` | 599 | yes |
| `added_tokens.json` | 306 | yes |
| `config.json` | 919 | yes |
| `configuration_phi3.py` | 10,411 | **no** — HF transformers modeling file; loader ignores .py. Skip. |

**Sizing fact (matters):** `model.onnx.data` = 2,131,292,928 B is **just under 2^31**
(2,147,483,648) — ~15.4 MiB headroom. Clears the UWP "≤2 GB file" limit. ORT mmaps
external data; a 64-bit process maps it fine.

**Budget reminder:** OgaCreateModel loads ~2 GB of weights to the DML device *at init*,
so the package **must be tagged as a Game** (≈4.05 GiB budget) before the run, not App
(≈1.18 GiB) — else init likely OOMs. (Game lever proven by the earlier probe.)

## Load-path strategy (bake into the probe)

1. Place all 3 DLLs at the **package root** (next to the exe). A packaged app's install
   dir is on the loader search path → cures the *only* prior failure (err 126 = absence),
   and bare-name resolution should now also work.
2. **Pre-load in dependency order by full path:** DirectML → onnxruntime →
   onnxruntime-genai, with `LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`
   (+ `SetDefaultDllDirectories`/`AddDllDirectory` on the install dir). Pre-loading
   DirectML.dll means ORT's later internal bare-name `LoadLibrary("DirectML.dll")` hits
   the already-loaded module.
3. Resolve OGA entry points via **`GetProcAddress`** (not link-time import) — precise
   per-symbol diagnostics, mirrors the existing probe's DMLCreateDevice approach, and the
   app still starts even if a DLL is absent. x64 = single calling convention, so fn-ptr
   sigs are safe regardless of the OGA_API_CALL macro.
4. Report **per step**: each DLL loaded? (else GetLastError dec+hex) · each OGA symbol
   found? · `OgaCreateModel` result + `OgaResultGetError` string on failure. Capture the
   **exact** error before hypothesizing (per prior-agent guidance).

Gate outcomes: (PASS) all load + OgaCreateModel succeeds → unblocks T2. (FAIL-A) a DLL
won't load → T1b: dumpbin imports to name the forbidden API. (FAIL-B) loads but
OgaCreateModel errors → capture the OgaResult string + any HRESULT.

## OPEN QUESTION the user flagged: model staging transport
Both dev-staging transports (SMB to `\\XBOX\D$`, or WDP `/api/filesystem/apps/file`
upload) push **from this PC**, so they need the bytes locally first. Avoiding a local
2 GB relay means on-device download (= T4's `BackgroundDownloader`, app code, not a dev
shortcut). Also unverified: whether the app's `LocalState` is even reachable on the
`D$` SMB share (if not, WDP upload is the path). **Resolve transport before downloading.**

## Probe harness facts (reuse)
- `xbprobe/` C++/CX UWP DX12 app. `Probe.cpp::RunCapabilityProbe` + `App.cpp` view.
  Status bits in `Probe.h`; report written to LocalState `probe-results.txt` via
  `CreateFile2`, pulled by `get-results.ps1`.
- Build/deploy: `build.ps1` → `deploy.ps1` → launch on console → `get-results.ps1`.
  Signing cert path + password come from `.env`. MaxVersionTested 22621 (ran fine).
- To add the DLLs to the appx: `<None Include="...dll"><DeploymentContent>true</DeploymentContent></None>`
  with `<Link>` to land them at package root.
