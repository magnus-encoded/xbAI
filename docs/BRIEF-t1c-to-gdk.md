# Brief: squeeze the cheap UWP path first, escalate to GDK only on hard evidence

**Date:** 2026-06-05 · **Author:** working notes · **Status:** decision brief, pre-T1c
**Sources:** `FINDINGS.md` (console-measured), `T1-NOTES.md` (stack/model choices),
Microsoft Learn GDK docs (cited inline), project memory (goal + constraints).

---

## 1. Where T1 left us (the one fact that forces this brief)

The stock desktop ORT-GenAI stack (onnxruntime-genai 0.6.0 / onnxruntime 1.20.1 /
DirectML 1.15.2) **loads and resolves all symbols inside the UWP app container** — the
big "does it even load?" unknown is answered YES (FINDINGS Run 3). But the gate itself
**failed at init**:

```
OgaCreateModel → 8007000E (E_OUTOFMEMORY)
DmlCommittedResourceAllocator.cpp(22)
```

with all 9 INT4 files staged at exact size and the package in the GAME profile
(budget **4.05 GiB**, status `0x17`). So the wall is **memory at init**, not API access.

The deeper reading (confirmed against docs, §5): the **4.05 GiB is the UWP/System-OS
sandbox ceiling, not the machine.** The same probe also reports a **cut-down GPU** in this
container — D3D feature level **11_0** and shader model **6.4** — versus the silicon's real
12_2 / 6.6. The UWP partition throttles *both* memory and GPU. That frames the whole
decision: we either make the small partition work, or we move partitions.

---

## 2. The fork

| Path | Gets us | Cost / risk |
|---|---|---|
| **A — stay UWP, cut init peak (T1c)** | Maybe Phi-3-mini INT4 running *today* on the easy ORT stack | Capped at ~4 GiB + cut-down GPU forever; may simply not fit |
| **B — GDK Game-OS title** | Full ~13.5 GB + full RDNA2 GPU (the project's actual goal) | Stock ORT no longer loads; needs NDA GXDK + custom DirectML; project-scale pivot |

**Rule for this brief: do A first because it is cheap and decisive either way.** A success
gives a shipping artifact on the easy path; a failure is the hard evidence that justifies
the expensive pivot to B. We do not start B on a guess.

---

## 3. Part A — the cheap test (T1c), run this first

**Objective:** determine whether the OOM is a *transient init-peak* artifact (fixable in
UWP) or a *true capacity wall* (only fixable by changing partition).

**Why it might be only a peak, not a wall:** `QueryVideoMemoryInfo` reported Local Budget
**4.05 / NonLocal 0.00**, so the 4 GiB shared pool *is* inside the budget — yet a 2 GB
weight commit failed. The likely peak at init is the sum of several transient buffers held
at once:

```
2 GB  mmap of model.onnx.data
2 GB  GPU DEFAULT-heap committed resource (the weights)         <- fails here
  ?   UPLOAD-heap staging copy during the weight upload
  ?   ORT arena reservation + KV-cache / intermediate tensors
-----
> 4.05 GiB peak  →  E_OUTOFMEMORY, even though steady-state would fit
```

**Steps (reuse the existing `xbprobe` harness — build.ps1 → deploy.ps1 → get-results.ps1):**

1. **Instrument the failure.** Call `QueryVideoMemoryInfo` immediately *before*
   `OgaCreateModel` and again in the failure branch; log `Budget` vs `CurrentUsage` at the
   moment of the OOM. This tells us the real headroom we hit, not the nominal 4.05.
2. **Cut the init peak** via ORT/GenAI knobs, cheapest first:
   - external weights as **mmap, no copy** (avoid the second in-RAM copy);
   - **`enable_mem_pattern=false`**, arena disabled / initial chunk shrunk;
   - any `genai_config.json` upload/mmap flag in the 0.6.0 schema.
3. **Floor test (isolation).** Push a *tiny* ONNX (a few MB) through the **same**
   `OgaCreateModel` path. If it inits, the code path is sound and the 2 GB model is purely
   a capacity problem — not a forbidden-API or config problem.

**Exit gates (decisive both ways):**

- **PASS** — Phi-3-mini INT4 inits within 4.05 GiB → we have a real on-device LLM on the
  UWP path. Capped, but shipping. Proceed to T2 (forward pass / tokens-sec). *Path B
  becomes a later "more headroom" upgrade, not a blocker.*
- **FAIL** — still OOMs after peak reduction, while the tiny model inits fine → **proven
  capacity wall.** The UWP partition is fundamentally too small. This is the evidence that
  authorizes Part B. (Fallback inside A before abandoning it: a smaller quant / shorter
  context / smaller-param model — but note this concedes the product goal of a capable
  model, so treat it as a stopgap, not the answer.)

**Cost:** ~1 session, no new SDKs, no NDA, no new transport. Pure reuse.

---

## 4. Part B — the GDK escalation plan (only if T1c FAILS)

Triggered only by a proven capacity wall. This is a partition change, and the docs make the
cost concrete:

> GDK games run on a separate **"Game OS"** with *"explicit guarantees about the GPU, CPU,
> and memory resources available to your game."* On Xbox the GDK supports **only D3D12.x**;
> D3D11/D2D/GDI/OpenGL are unsupported. Games compile against **`WINAPI_FAMILY_GAMES`**,
> link **`xgameplatform.lib`** with **`/NODEFAULTLIB`**, and package as **MSIXVC**.
> *(learn.microsoft.com/gaming/gdk — "What is the GDK (TL;DR)")*

**The hard consequence:** the Game OS is **not Win32-complete**, so the **stock desktop
ORT-GenAI stack we proved loads in T1 will NOT load there.** Everything T1 validated is
specific to the UWP/desktop CRT environment. Plan around that.

**Phase B0 — Access & toolchain (gating, do before any code).**
- Acquire the **GXDK (Gaming eXtensions Dev Kit / "Xbox Extensions")** — this is the
  console half and is **NDA**, gated behind registered Xbox developer access. The public
  GRDK alone is *not* enough for console. **Resolve this first; it can block for weeks.**
- Install matching Windows 10 SDK (per GDK version) + VS2019/2022 GDK integration; confirm
  the `xb*` remote tools + PIX-for-Xbox talk to the devkit (we already reach it at
  `XBOX`/192.168.1.233:11443 over WDP; GDK uses its own `xbapp`/`xbconnect` path).
- Build + deploy the **GDK "BasicGame" template** to the console as a smoke test. Success =
  Game OS partition reachable; capture its **actual** `QueryVideoMemoryInfo` to confirm the
  ~13.5 GB budget and the full 12_2 / SM 6.6 GPU (closes the loop on §1's UWP-vs-silicon gap).

**Phase B1 — Inference engine decision (the fork inside B).**
- **B1-a: port ONNX Runtime to the GDK target.** Rebuild ORT (+DML EP) against
  `WINAPI_FAMILY_GAMES` / D3D12.x. Upside: reuse the ONNX model + tokenizer we already
  staged. Downside: large, unproven build; ORT isn't shipped for Game OS; high risk.
- **B1-b: hand-written DirectML inference (FINDINGS "Path A").** DirectML *is* available to
  DX12 titles and is explicitly designed to be embedded in a game's render loop (DirectML
  docs: create DML device alongside D3D12, upload weight tensors as D3D12 resources via an
  upload heap, record operators into command lists). We control every allocation — we can
  deliberately stream weights and place them to fit the real budget. Aligns with the project
  constraint **D3D12/DirectML only (no CUDA/Vulkan)**. Prior art to lean on: Microsoft
  `DirectML` LLM sample (Phi-2/3, LLaMA) and `Const-me/Whisper`.
- **Recommendation:** prototype **B1-b** first — it's the most controllable and the most
  aligned with the goal; fall back to B1-a only if reimplementing the Phi-3 graph proves
  too costly. Validate on a *single transformer block* before the full model.

**Phase B2 — Re-run the load+init gate, GDK edition.** Repeat T1's structure (load → init →
weights resident on the GPU) but in the Game OS, with whichever engine B1 picks. This is the
GDK analogue of the gate we just ran; same pass/fail discipline, new partition.

**Phase B3 — T2 onward** (forward pass, tokens/sec) proceeds on the unblocked engine.

**Open unknowns to retire early in B (cheap reads, don't let them lurk):**
- GXDK access timeline (B0) — the true critical path.
- Whether DirectML's Game-OS build matches the redist DML feature level we need.
- Model transport into a Game-OS title's storage (MSIXVC packaging vs the WDP/LocalState
  upload we used for UWP — different mechanism; the 2 GB `model.onnx.data` may ship inside
  the package or via the dev scratch **drive D:** noted in GDK storage docs).

---

## 5. Context map — what ties to what

Explicit trace from the evidence in this window to each conclusion above, so the reasoning
is auditable and survives a compaction:

| Conclusion | Drawn from |
|---|---|
| "4 GiB is the *partition* limit, not the chip" | FINDINGS: budget 4.05 GiB + GPU shown as **FL 11_0 / SM 6.4** (cut down from 12_2 / 6.6) — two independent throttles point at the container, not silicon |
| "The OOM may be an init *peak*, not a wall" → T1c | FINDINGS: `QueryVideoMemoryInfo` Local **4.05 / NonLocal 0.00** (shared pool *is* in budget) yet a 2 GB commit failed → transient double-buffering is the leading hypothesis |
| "Stock ORT works here but only here" | FINDINGS Run 3 (loads in UWP w/ bundled desktop CRT) **vs** GDK docs (Game OS = `WINAPI_FAMILY_GAMES`, no full Win32) → the load success is environment-specific |
| "GDK is the only route to the full machine" | GDK docs: Game OS gives *"explicit guarantees about GPU, CPU, and memory"* — the documented home of the ~13.5 GB + full GPU |
| "If we pivot, prefer custom DirectML" | Project memory constraint **D3D12/DirectML only, no CUDA/Vulkan** + DirectML docs (game-embeddable, manual resource control) + FINDINGS prior art (MS DirectML LLM sample, Const-me/Whisper) |
| "Do the cheap test first" | The two paths' cost asymmetry: T1c = 1 session, no NDA, full harness reuse; GDK = NDA access + new toolchain + new inference engine. A's result is decisive for B either way |
| Harness + transport we'll reuse for T1c | `T1-NOTES.md` harness facts + memory `xbprobe-build-deploy` + `xbox-devkit-connection` (WDP at `XBOX`:11443, LocalState upload already working) |

---

## 6. Immediate next action

Run **T1c** (§3) on the existing harness. Record results in `FINDINGS.md` under a new
"Run 5 — T1c" heading. The exit gate there — PASS (ship on UWP, go T2) or FAIL (capacity
wall proven, open Part B / Phase B0) — is the next real decision point.
