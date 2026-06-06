# core/ — the platform-agnostic brain

Plain C++. **Zero** WinRT (`winrt/…`, `Windows::`) and GDK (`XGame*`, `XCurl`,
`GameInput`) includes — this is a hard review gate (ADR-0004 §5 / ADR-0006 §6). A
platform include here means a seam is missing: add an interface in `core/pal/` and push
the implementation down to `pal/<backend>/`.

`core/` builds as a **consumable static library** (ADR-0005); each shell links it.

The rule for "does this belong in core?" (ADR-0006 §1): **default yes.** It only leaves
core when the metal *forces* a per-platform difference.

## Modules
| Dir | Responsibility | Filled by task |
|---|---|---|
| `pal/` | the **interface contracts** core calls (the seams). Frozen by P0. | P0 |
| `inference/` | OGA C-API wrapper: load model from a path, run the decode loop, yield tokens | core-inference |
| `server/` | HTTP/1.1 parse + OpenAI `/v1/chat/completions` + SSE, over `ISocketListener` | core-server |
| `queue/` | single-flight FIFO, request lifecycle, 429/503 on overflow; owns the decode worker thread | core-queue |
| `model/` | model-dir layout, `genai_config.json`, model-id → path resolution | core-model |

See `docs/adr/0004`, `0006` for the architecture and the philosophy.
