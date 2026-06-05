# xbAI

Getting an open-weight LLM to run on a retail Xbox Series X (in Developer Mode),
on the GPU, and serve it over the LAN as an OpenAI-compatible API. The console only
lets you deploy UWP apps, so everything here runs inside that sandbox using
DirectML/D3D12.

This started as a "can this even work?" weekend experiment. Turns out it does: a
quantized Phi-3-mini loads, runs on the Series X GPU, and decodes correct tokens
(~2.3 tok/s right now). I couldn't find anyone else reporting an LLM actually
decoding on Xbox GPU silicon in retail dev mode, so I'm putting it up.

Right now the repo is mostly the **probe** I used to prove out the hard parts, plus
my notes and design decisions for the actual server that comes next.

## What works so far

Each unknown got its own throwaway test on the real console (raw output in
[FINDINGS.md](FINDINGS.md)):

- **T1** - the stock ORT-GenAI / ONNX Runtime / DirectML DLLs load and initialize
  inside the UWP app container (this was the one I expected to fail).
- **T2** - a forward pass runs on the Xbox GPU and produces correct output. ~2.3
  tok/s steady-state decode.
- **T3** - a UWP app tagged as a "Game" can accept inbound LAN TCP; curl from my PC
  gets HTTP 200 back in ~21 ms.
- **T4** - it can download a model straight from Hugging Face on the console itself
  (the full 2 GB Phi-3-mini set), and resume if the app restarts mid-download.

So the whole chain - download, load, run on GPU, serve over LAN - holds together.
Next up is turning T3 into a real single-flight `/v1/chat/completions` server with a
little on-console dashboard. See [TASKS.md](TASKS.md).

One caveat worth knowing: UWP dev mode only hands you a slice of the machine (~4 GiB,
feature level 11_0). The full hardware lives in the GameCore/GDK world, which is a
separate port behind a partner account, so that's deliberately parked for later -
[ADR-0004](docs/adr/0004-portability-uwp-to-gamecore.md) has the reasoning.

## Running it

You'll need an Xbox Series X in Developer Mode on your LAN, and a Windows box with
Visual Studio 2022 (the "Universal Windows Platform development" workload + C++ v143
UWP tools).

```powershell
git clone https://github.com/magnus-encoded/xbAI ; cd xbAI

Copy-Item .env.example .env     # then edit .env: your console host, cert, model
.\Test-Requirements.ps1         # sanity-checks your toolchain + the console
.\Start-Build.ps1               # restore deps, build the appx, deploy it
```

Then on the console: Dev Home > My games & apps > set the probe to content type
**Game** (this is what unlocks DX12 + the bigger memory budget), launch it, and pull
the report back with `.\xbprobe\get-results.ps1`.

Two things aren't in the repo and you supply yourself:

- **A signing cert** - make a self-signed `.pfx` with publisher `CN=xbAI` (matching
  `Package.appxmanifest`) and point `.env` at it.
- **Model weights** - multi-GB, grab your own from Hugging Face under its license.
  See [xbprobe/model-stage/README.md](xbprobe/model-stage/README.md). The native
  runtime DLLs get pulled from NuGet by `Get-Dependencies.ps1`.

All the machine-specific stuff (console IP, cert password, SMB creds) lives in a
gitignored `.env` - nothing real is committed, `.env.example` is the template.

## Repo layout

```
.env.example         copy to .env and fill in
Load-Env.ps1         tiny .env loader the scripts share
Test-Requirements.ps1 / Start-Build.ps1   the two things you run
CONTEXT.md           glossary, hardware facts, what's decided and why
FINDINGS.md          raw probe output from the console
TASKS.md             what's done / what's next
docs/adr/            the design decisions
xbprobe/             the probe itself (C++/CX UWP DirectX app)
```

If you're poking at this with a coding agent, there's a `CLAUDE.md` that gives it the
repo map and the one real gotcha (there's a single physical console, so don't deploy
to it from two places at once).

## Notes

Not affiliated with Microsoft; "Xbox" is theirs. You need a console you've legitimately
put into Developer Mode, and you're on the hook for the licenses of whatever model you
run. It's a personal experiment, no warranty, expect rough edges.
