# CLAUDE.md — agent orientation

You are helping run/extend an LLM **on a retail Xbox Series X in Developer Mode**.
Read this first, then `CONTEXT.md` (glossary + hardware facts) and `docs/adr/`
(decisions + rationale).

## The one safety rule

There is **one physical console** on the user's LAN. **Serialize deploy + launch**:
only one app installed/activated/running at a time. A redeploy needs a **version
bump** in `Package.appxmanifest` (same-version install is blocked). To clear a
wedged activation, uninstall + reinstall.

## How to do anything

Everything environment-specific is in a gitignored **`.env`** (copy from
`.env.example`). Never hardcode the console host, cert password, or SMB creds —
read them from `.env` via `Load-Env.ps1`. Never commit `.env`, `*.pfx`, `*.cer`,
model weights, or the console export files (already in `.gitignore`).

```powershell
.\Test-Requirements.ps1     # preflight: toolchain, deps, console reachability
.\Start-Build.ps1           # restore native deps -> build signed appx -> deploy
.\xbprobe\get-results.ps1   # pull probe-results.txt back from the console
```

Build chain (under `xbprobe/`): `Get-Dependencies.ps1` (NuGet→redist) →
`build.ps1` (MSBuild, must pick the VS install with C++/CX UWP targets) →
`deploy.ps1` (Device Portal REST upload) → launch on console → `get-results.ps1`.

## Console access (host/port/creds in `.env`)

- **Windows Device Portal** (WDP): `https://<XBOX_HOST>:<WDP_PORT>`, self-signed
  cert → `curl -k`, **no auth**. POST/upload/DELETE need the `CSRF-Token` cookie
  echoed back as the `X-CSRF-Token` header (see `deploy.ps1`).
- **Launch:** `POST /api/taskmanager/app?appid=<b64 PRAID>&package=<b64 PFN>`
  with `Content-Length: 0`. **Terminate:** `DELETE /api/taskmanager/app?package=<b64 PFN>`.
- **Pull files:** `/api/filesystem/apps/file?knownfolderid=LocalAppData&...` — the
  `path` needs a leading backslash (`\LocalState`) or WDP returns an empty listing.
- **SMB fast-push:** `\\<XBOX_HOST>\D$` (creds in `.env`).

## The two things a fresh clone must supply

1. **Signing cert** — a self-signed `.pfx` (publisher `CN=xbAI`, matching the
   manifest), referenced from `.env`.
2. **ONNX model weights** — fetched per-dev from Hugging Face; see
   `xbprobe/model-stage/README.md`.

## Status (mid-2026)

The four de-risking probes (T1 load/init, T2 GPU decode, T3 LAN serve, T4 on-device
HF download) all **PASS** — see `FINDINGS.md`. The product build (single-flight
OpenAI-compatible server + dashboard) is unblocked. Architect it
**portable-by-construction** per ADR-0004: a plain-C++ `core/` (zero WinRT/GDK
headers) over a thin `pal/`, so the eventual GameCore/full-metal move is a flip,
not a rewrite. Language is locked to **C++**.
