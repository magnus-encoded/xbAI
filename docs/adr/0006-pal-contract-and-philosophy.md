# ADR-0006: PAL contract & philosophy — thin PAL, tolerated duplication, deferred abstraction

- Status: **Accepted**
- Date: 2026-06-06

## Context
ADR-0004 split the product into a portable `core/` and a per-platform `pal/`, and listed
the seam *inventory*. Before fanning the work out to parallel agents we must freeze the
**contract** they code against, and — more importantly — the **judgment rule** every agent
applies when deciding "does this code go in `core/` or `pal/`?" Without a shared rule,
parallel work drifts: the PAL accretes things that did not need to be platform-specific,
and premature shared abstractions ossify the wrong seams.

## Decision

### 1. PAL minimality — inverted default
Code lives in **`core/` by default**. It moves to `pal/` only when the **metal forces a
per-platform divergence**. The test is **"must this differ per platform?"** — *not*
"could this be platform-specific?". Keep pulling code into `core/` until the platform
forces it out. The PAL is for *necessary adaptation to different metal*, nothing more.

### 2. Concurrency ownership follows minimality
`std::thread` is portable, so **`core/` owns the worker threads** — the decode worker and
the single-flight FIFO queue (ADR-0002). **`pal/` owns only the platform event pumps it is
forced to**: the UI message loop and the socket accept loop. Threading is not a seam;
keeping it out of the PAL is rule #1 in action.

### 3. Duplication between PAL backends is acceptable
**No premature `pal/common`, no shared base classes "to be safe."** `pal/uwp` and
`pal/gdk-desktop` may repeat each other freely. Duplication across backends is cheaper than
the wrong abstraction.

### 4. Abstraction/DI is deferred, evidence-driven
Extract a shared seam **only from observed, repeated, same-shaped need across ≥2 backends**
— never up front. A wrong abstraction is costlier to unwind than duplication is to tolerate.
Dependency injection *later* beats over-eager DRY *now*.

### 5. The frozen seam set
The PAL surface is the minimal set forced by the metal. Interface signatures are **frozen by
the P0 bootstrap task** (see `TASKS.md`) and then treated as the contract:

| Seam (interface in `core/pal/`) | What core needs from it |
|---|---|
| `ILifecycle` / entry | start/stop, suspend/constrain/resume hooks (PLM) |
| `ISocketListener` | accept inbound TCP, hand core a byte stream per connection |
| `IModelStore` | resolve the model-root path for a model-id; enumerate/stat files |
| `IDownloader` | fetch a **file set** to a directory, resumable, with progress |
| `IInput` | report the "press-A" intent (focus + activate) |
| `IDashboard` | present status (state, endpoint URL, tokens/s, queue depth) |

The set may shrink further under rule #1; it does not grow without a demonstrated metal
divergence.

### 6. Enforcement (carried from ADR-0004 §5)
`core/` **must not** `#include` any WinRT (`winrt/…`, `Windows::`) or GDK (`XGame*`,
`XCurl`, `GameInput`) header. This is a hard review gate. A platform include appearing in
`core/` means a seam is missing — add the interface, push the implementation down to
`pal/<backend>/`.

## Consequences
- **Fewer shared abstractions ⇒ easier parallelization**: the only cross-agent coordination
  point is the frozen seam signatures (#5); within a backend, agents are free.
- Some duplication is carried deliberately; it is a refactor target *later*, from evidence,
  not a defect.
- `core/` stays portable and testable on the GPU-less dev box and on CI, decoupled from any
  shell — which is the property the whole UWP→GameCore staircase depends on.
