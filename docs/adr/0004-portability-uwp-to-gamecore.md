# ADR-0004: UWP now, GameCore as the full-metal upgrade — portability via a thin Platform Abstraction Layer

- Status: **Accepted**
- Date: 2026-06-05

## Context
The probes (FINDINGS T1c/T2/T3/T4) prove the whole pipeline runs on a retail console
in **UWP dev mode** — but under that environment we reach only a fraction of the
machine: **~4.05 GiB** memory budget and **Feature Level 11_0 / SM 6.4**, with the
DirectML INT4 packed-math path unconfirmed and decode at a modest 2.31 tok/s. The full
machine (~13.5 GB, real feature level, DirectStorage, packed INT4/INT8, all 8 cores)
lives in the **GameCore / GDK "Title" environment**, which is a *different runtime and
application model*, not a permission toggle.

Reaching GameCore requires the **GDK with Xbox Extensions (GDKX)**, gated behind
**ID@Xbox** onboarding on a **Business** Partner Center account (business identity
verification via DUNS or documents; no fee, but an approval + entity-registration
process). We want to **delay that bureaucracy and signup as long as possible** —
program terms may also change before we need full-machine access — **without** painting
ourselves into a UWP corner. The goal: when the partner account lands, going full-metal
is a *flip*, not a rewrite.

Two facts make a clean staging possible:
1. The expensive, load-bearing risk (does ORT-GenAI + DirectML load, init, decode,
   serve, and download on this silicon?) is **already retired** and is **platform-core,
   not platform-shell** — it ports verbatim.
2. The **public PC GDK** (`microsoft/GDK`, free, no gatekeeping) builds the
   `Gaming.Desktop.x64` GameCore target. GDKX is described as an *add-on* to that same
   base — "largely Graphics, Media, and Storage specific." So the GameCore *app model*
   can be developed and validated **on Windows for free**, before any partner account,
   and the Xbox delta is small and well-scoped.

## Decision

### 1. Two-tier product, bureaucracy deferred
- **v1 ships on UWP** on the retail dev unit (ADR-0001/0002/0003 hold). This is a real,
  usable product on its own and needs **only** the ~$19 Dev-Mode Partner Center
  registration we already have.
- **GameCore is the full-metal upgrade**, pursued only when full machine is actually
  needed. **Do not** start ID@Xbox / Business-account onboarding until then.

### 2. Architect as portable Core + thin Platform Abstraction Layer (PAL)
Structure the product so platform-specific code is quarantined behind narrow interfaces:

- **`core/` — platform-agnostic, zero WinRT/GDK headers.** The inference engine
  (ORT-GenAI / onnxruntime / DirectML via the OGA C API), the HTTP/1.1 + OpenAI
  `/v1/chat/completions` + SSE protocol logic, the single-flight FIFO queue and request
  lifecycle, model-dir layout/config handling, tokenisation orchestration. This is the
  bulk of the code and is **identical on every backend**.
- **`pal/` — one thin implementation per platform** of a small set of interfaces for the
  seams (see inventory below). Core depends only on the interfaces; `main`/entry wires in
  the concrete PAL for the current build.

### 3. Three PAL backends over time
| Backend | When | Cost / gate | Purpose |
|---|---|---|---|
| `pal/uwp` | now | already have | Ship v1 on the retail dev unit. |
| `pal/gdk-desktop` | next (free) | **none** — public PC GDK | Prove the GameCore app model + Win32 API swaps **on Windows**, at zero gatekeeping. De-risks the port before any partner account. |
| `pal/gdk-xbox` | flip-the-switch | ID@Xbox + GDKX | Full console metal. Reuses ~all of `gdk-desktop`; the delta is the Xbox **Graphics/Media/Storage extensions** + the lifted memory/feature-level. |

`gdk-desktop` is the key move: it makes the "GameCore port" ~90% done and tested for
free; the Xbox flip becomes a build-config change (`Gaming.Desktop.x64` →
`Gaming.Xbox.Scarlett.x64`) plus the small extension-specific surface.

### 4. Product language resolved on portability grounds: **C++**
CONTEXT left host language open ("decide on capability grounds when the product
starts"). Portability decides it: **C++** — a plain-C++ `core/`, **C++/WinRT** for the
`pal/uwp` shell (never deprecated C++/CX), **Win32 C++** for the GDK shells. The
inference core is already C++ (OGA C API); GDK/GameCore is a **C++-first** SDK. C# is
**rejected** for the product shell because it complicates the GameCore-native port
(adds a managed-runtime dependency exactly where we want a clean Win32/GDK target).

### 5. Enforcement discipline
- `core/` **must not** `#include` any WinRT (`winrt/…`, `Windows::`) or GDK (`XGame*`,
  `XCurl`, `GameInput`) header. This is a review gate; a violation means a seam belongs
  in the PAL.
- All platform APIs live **only** under `pal/<backend>/`. New platform dependencies get
  a PAL interface first, then per-backend implementations.

## Seam inventory (the PAL surface)
Each row is one narrow interface in `core/` with per-backend implementations. UWP today;
GDK columns are the planned swaps (validated free on `gdk-desktop`, Xbox-finalised on
`gdk-xbox`).

| Seam (PAL interface) | `pal/uwp` (WinRT) | `pal/gdk-*` (Win32/GDK) |
|---|---|---|
| App lifecycle / entry | `CoreApplication` / `IFrameworkView` | `WinMain` + `XGameRuntimeInitialize` |
| Package manifest (build artifact) | `Package.appxmanifest` | `MicrosoftGame.config` |
| Inbound HTTP listener | `StreamSocketListener` | **Winsock** (no AppContainer ban) |
| Model downloader (resumable) | `BackgroundDownloader` | **XCurl / libHttpClient** (or WinHTTP) |
| Storage / model root path | `ApplicationData::LocalFolder` | GameCore known-folder / Win32 path |
| Dashboard / UI presenter | **XAML** + data-binding | Win32 + own D3D overlay / GDK-supported UI |
| Gamepad input (focus + A) | `Windows.Gaming.Input` | **GameInput** (GDK) |
| Video-memory budget query | DXGI `QueryVideoMemoryInfo` | same DXGI + title memory APIs |
| GPU device | core D3D12 (shared) | core D3D12 **+ Xbox D3D12.x extensions** |

Note the AppContainer tax we paid in the probes (bundling desktop `vcruntime/msvcp`,
loopback-ban gymnastics) **disappears** under Win32 GameCore — the port removes
constraints, it doesn't add them.

## What "flip the switch" concretely means
1. Obtain ID@Xbox + GDKX (the only hard gate).
2. Add the `Gaming.Xbox.Scarlett.x64` build config; swap `pal/gdk-desktop` →
   `pal/gdk-xbox` (mostly shared code + the Graphics/Media/Storage extensions).
3. Raise the model-residency targets to the GameCore memory budget; enable the real
   feature level / packed INT4 path; optionally DirectStorage for the 2 GB weight load.
4. `core/` and the OpenAI server are **unchanged**.

## Consequences
- **Bureaucracy is deferred to the last responsible moment** while portability is paid
  for up front, cheaply (interfaces are small; the seams are already enumerated).
- The product gains a **free Windows build** (`gdk-desktop`) usable for fast iteration
  and CI of the core, independent of console availability.
- A modest up-front cost: v1 routes platform calls through interfaces instead of calling
  WinRT directly. Given the seam list is short and the core dwarfs the shells, this is a
  small tax for a de-risked upgrade path.
- **Language is now locked to C++** (resolves the CONTEXT open question); the product
  bootstrap and all backlog UI/serving tasks assume the `core/` + `pal/` split.
- Full-metal throughput/memory gains (8B-class models, real feature level, packed
  INT4, DirectStorage) become an **additive flip**, consistent with the north-star
  "best model the hardware can serve."
