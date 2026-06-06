# core/pal/ — the PAL contract (interfaces only)

These are the **only** seams between the portable `core/` and the platforms. Pure
abstract interfaces; **no implementations live here** (those go in `pal/<backend>/`).

The set is the minimum **forced by the metal** (ADR-0006 §1). It may shrink; it does not
grow without a demonstrated per-platform divergence.

**Signatures are FROZEN by the P0 bootstrap task** and then treated as the contract every
parallel task codes against. Do not change a signature after P0 without coordinating —
that is the one cross-agent coupling point.

## Seams (target — finalized by P0)
- `ILifecycle` / entry — start/stop, suspend/constrain/resume (PLM) hooks
- `ISocketListener` — accept inbound TCP, hand core a per-connection byte stream
- `IModelStore` — resolve model-root path for a model-id; enumerate/stat files
- `IDownloader` — fetch a **file set** to a dir, resumable, with progress
- `IInput` — report the press-A intent (focus + activate)
- `IDashboard` — present status (state, endpoint URL, tokens/s, queue depth)

Concurrency note (ADR-0006 §2): threads are **not** a seam. `core/` owns the worker
threads; PAL owns only the platform event pumps (UI message loop, socket accept).
