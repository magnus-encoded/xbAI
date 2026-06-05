# xbAI Capability Probe

A throwaway, minimal UWP **DirectX 12** app whose only job is to ask the Xbox
Series X (in Developer Mode) what the **game container actually exposes** — so we
stop guessing from desktop docs and get ground truth from *our* console.

It checks, on launch:

1. **DXGI** – enumerate the adapter (name, dedicated/shared memory).
2. **D3D12** – create a device; report feature level, highest shader model, UMA.
3. **Video-memory budget** – live `QueryVideoMemoryInfo` (this is the real test
   of whether the *game* memory profile gave us the big pool vs the ~1 GB app pool).
4. **DirectML** – `LoadLibrary("DirectML.dll")` + `DMLCreateDevice` *inside the
   sandbox*, plus its max feature level. This is the pivotal unknown.
5. **WinML** – construct `LearningModelDevice(DirectX)`, the in-box proxy for
   "will the ORT/WinML LLM stack initialise here?".

## Output

- **On the TV** – the whole screen is cleared to a status colour:
  - 🟩 green  = everything came up (incl. WinML)
  - 🟦 teal   = D3D12 **+ DirectML** up (the part we need) — WinML didn't
  - 🟧 orange = D3D12 up but **DirectML failed**
  - 🟥 red    = no D3D12 device
- **`probe-results.txt`** – the full report, written to the app's LocalState and
  also emitted via `OutputDebugString`. Pull it over the Device Portal (below).

## Prerequisites (build machine)

This box currently has **no** GDK/VS toolchain. Install:

- **Visual Studio 2022** with the **"Universal Windows Platform development"**
  workload, **C++ (v143) UWP tools**, and a **Windows SDK** (10.0.22621 used by
  the project; retarget via right-click → *Retarget solution* if you have a
  different one). C++/CX (`/ZW`) is used, so **no NuGet restore is required.**

## Build & deploy

Two ways:

**A. F5 remote deploy (fastest loop)**
1. Open `xbprobe.sln`, set config to **Release | x64** (or Debug).
2. Project → Properties → Debugging → **Debugger to launch: Remote Machine**,
   **Machine Name: `XBOX`** (or `192.168.1.233`), Authentication: **Universal (Unencrypted)**.
3. **F5**. VS creates a temporary signing cert on first build, deploys, and launches.

**B. Sideload via Device Portal**
1. Project → **Publish → Create App Packages → Sideloading** → produces an
   `.msix`/`.appx` (+ a generated test cert).
2. Browse `https://XBOX:11443` → **My games & apps → Add** → upload the package.
3. Launch it from the console (or via the portal).

## Tag it as a Game (to unlock DX12 + full GPU + ~5 GB)

After install, on the console open **Dev Home → My games & apps**, select
**xbAI Capability Probe → … → Configure / View details**, and set the content
type to **Game** (vs App). Re-run the probe — the *budget* line should jump from
~1 GB into the multi-GB range, flipping the on-screen colour from orange/black
toward teal/green. That diff is exactly the capability we're confirming.

## Pull the results back over the network

After the app has run once:

```powershell
$B = "https://XBOX:11443"
# 1. Find the installed package's full name
$pfn = (curl.exe -k -s "$B/api/app/packagemanager/packages" |
        ConvertFrom-Json).InstalledPackages |
        Where-Object { $_.PackageFamilyName -like "xbAI.Probe*" } |
        Select-Object -First 1 -ExpandProperty PackageFullName
$pfn

# 2. (optional) list files in LocalState
curl.exe -k -s "$B/api/filesystem/apps/files?knownfolderid=LocalAppData&packagefullname=$pfn&path=\"

# 3. Download the report
curl.exe -k -s "$B/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$pfn&filename=probe-results.txt" -o probe-results.txt
Get-Content probe-results.txt
```

## What we do with the answers

The `[BUDGET]`, `[DML]` and `[WinML]` lines decide the inference backend fork:
**A** = ORT-GenAI + DirectML via WinML, **B** = raw DirectML graph, **C** =
custom D3D12 compute kernels. See project memory `xbox-ai-project` for the map.

> Nothing here is meant to be pretty — it's a baseline to start asking the
> hardware real questions.
