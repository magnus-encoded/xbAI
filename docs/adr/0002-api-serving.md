# ADR-0002: API serving = StreamSocketListener + hand-rolled HTTP, LAN-only, OpenAI-compatible

- Status: **Accepted**
- Date: 2026-06-04

## Context
The product must expose the on-device model as an HTTP API ("shows an API
endpoint"). Three UWP constraints shape this:
- The app runs in an **app container**; **loopback is blocked by default** (no
  same-machine `127.0.0.1` access without a dev exemption), but **cross-machine
  inbound** is allowed with the right capability.
- UWP has **no inbox HTTP *server*** — `Windows.Web.Http` is client-only.
- The manifest currently declares only **`internetClient`** (outbound), which
  cannot accept inbound connections.

A native HTTP server lib (cpp-httplib etc.) would call Winsock directly and carry
the **same app-container forbidden-API risk** we're already managing for the
ORT-GenAI native stack (ADR-0001) — a problem we don't need to duplicate.

## Decision
- Serve with **WinRT `StreamSocketListener` + a minimal hand-rolled HTTP/1.1
  handler** — app-container-legal *by construction* (pure WinRT), no native HTTP
  dependency.
- **LAN-only**: add **`privateNetworkClientServer`** to the manifest; keep
  `internetClient` for Hugging Face downloads. **Do not** add
  `internetClientServer` — internet exposure is out of scope and is the operator's
  reverse-proxy responsibility.
- Treat clients as living on **other LAN devices** hitting
  `http://<console-ip>:<port>`, never the console itself — this sidesteps the
  loopback ban entirely.
- Expose an **OpenAI-compatible `/v1/chat/completions`** so any existing client/SDK
  works with only a base-URL change (this *is* the demo).
- **Single-flight concurrency**: the ~4 GiB budget fits exactly one KV cache and
  there is one GPU, so the server is a **FIFO-serialized single-worker queue** —
  concurrent requests are accepted at the socket but generations run one at a time;
  **SSE** streams the active job; a **bounded queue returns 429/503** on overflow.
  Continuous batching (vLLM-style) is memory-impossible at this budget and is
  explicitly out of scope for v1.
- **Process shape:** one process, three threads — **XAML UI thread** (dashboard +
  controller input), decode loop (worker), socket accept+serve (worker); requests
  enqueue prompts to the decode worker. DirectML runs headless on its own D3D12
  device, not on the UI thread.

## Consequences
- We own a small, correct HTTP/1.1 parse/response path (and SSE streaming if we
  want token streaming) — modest code, but zero native-lib container risk.
- No same-machine API consumption; the dashboard *displays* the endpoint, it does
  not call it over loopback.
- Internet reachability, TLS, and auth are deliberately **not** provided; an
  operator who wants them fronts the console with a reverse proxy.
