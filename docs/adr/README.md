# Architecture Decision Records — index

| ADR | Title | Status | One-line |
|-----|-------|--------|----------|
| [0001](0001-inference-backend.md) | Inference backend = ORT-GenAI / WinML + DirectML | Accepted (provisional) | Primary bet is **ORT-GenAI** (out-of-box native stack) for its decode loop; raw WinML is a disguised Path B, not a cheap fallback. The real fork is "**does the ORT-GenAI native stack load + init in UWP?**" — gate is one token on the Xbox GPU. Empirical; revisit on probe data. |
| [0002](0002-api-serving.md) | API serving = StreamSocketListener + hand-rolled HTTP, LAN-only, OpenAI-compatible | Accepted | Serve over **WinRT `StreamSocketListener` + hand-rolled HTTP/1.1** (no native HTTP lib → no app-container risk). **LAN-only** (`privateNetworkClientServer`), cross-machine only (loopback ban is moot), **OpenAI-compatible `/v1/chat/completions`**, **single-flight FIFO** queue + SSE. No internet exposure (user's reverse-proxy problem). |
| [0003](0003-v1-scope-dev-tool.md) | v1 is a developer tool with build-time model config | Accepted | v1 targets **experienced devs on dev units**: *which* model is fixed at **build/config time**, but the on-console **press-A → download → serve** flow is kept (for that one model). Deferred: multi-model *selection*, catalog/browse, naive-user packaging. |
| [0004](0004-portability-uwp-to-gamecore.md) | UWP now, GameCore as the full-metal upgrade — portability via a thin PAL | Accepted | UWP gives a *fraction* of the metal (4 GiB / FL 11_0); full machine = **GameCore/GDKX**, gated behind ID@Xbox (**defer the bureaucracy**). Architect as portable **`core/`** (zero platform headers: inference + OpenAI server + queue) over a thin **`pal/`** (lifecycle, sockets, downloader, storage, UI, input). Three backends: `uwp` (now) → `gdk-desktop` (**free** PC GDK, validates the port) → `gdk-xbox` (the flip). **Language locked = C++** on portability grounds. |

See [`../../CONTEXT.md`](../../CONTEXT.md) for the glossary + current state, and
[`../../FINDINGS.md`](../../FINDINGS.md) for measured console facts.
