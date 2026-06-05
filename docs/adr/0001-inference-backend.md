# ADR-0001: Inference backend on Xbox = ONNX Runtime GenAI / WinML + DirectML

- Status: **Accepted** (provisional — pending on-device forward-pass proof)
- Date: 2026-06-04

## Context
The Xbox Series X in retail Dev Mode exposes **only D3D12 / DirectML** for GPU
compute — no CUDA, ROCm, or Vulkan driver. Deployment is **UWP-only** (no Win32+GDK
GameCore on a non-activated devkit). The capability probe (`FINDINGS.md`) measured,
on the real console, that:
- a D3D12 device creates (FL 11_0, Shader Model 6.4, UMA),
- **WinML `LearningModelDevice(DirectX)` succeeds in-sandbox** (DML substrate is reachable),
- `LoadLibrary("DirectML.dll")` by bare name fails (err 126),
- the Game memory budget is ~4 GiB.

Upstream, Microsoft already provides DirectML LLM samples and **ORT-GenAI** with
pre-fused DML graphs (RoPE/MHA/MLP), and **pre-quantized INT4 ONNX models** for it
exist on Hugging Face.

## Decision
Pursue **Path A**, with the runtimes disambiguated: the **primary** bet is
**ONNX Runtime GenAI** (out-of-box native stack) with the DirectML execution
provider, because it supplies the autoregressive decode loop, KV-cache, sampling,
and pre-fused DML graphs we'd otherwise hand-roll. The probe's proven WinML success
de-risks this by showing the **DML substrate is reachable** — it does *not* prove
ORT-GenAI loads in the container. Raw **inbox WinML** (single forward pass only) is
*not* a cheap fallback: hand-rolling a correct KV-cached decode loop on it is ~as
much work as Path B, so it collapses into Path B. The real fork is therefore
"**does the ORT-GenAI native stack load + init in UWP: yes/no**." Choice is
explicitly **empirical** — revisit once probe data exists. First target model:
**Phi-3-mini INT4 (~2 GB)**.

## Alternatives considered
- **Path B — author the transformer as a raw DirectML operator graph.** More code,
  but removes the ORT-in-UWP dependency. Fallback if ORT-GenAI won't initialise or
  run inside the UWP container.
- **Path C — custom D3D12 compute kernels** (à la `Const-me/Whisper`; fill the open
  llama.cpp DirectML-backend gap). Maximum control/effort. Reserve for if both A
  and B prove unworkable, or for performance beyond what DML delivers.

## Consequences
- **Need to resolve DLL loading:** rely on inbox WinML, or package the DirectML
  redist (`Microsoft.AI.DirectML`) and load by full path from the app install dir
  (bare-name load is proven to fail).
- **Model format:** consume ONNX/INT4 from Hugging Face directly for v1 — no
  conversion pipeline needed initially.
- **Memory-bound:** ~4 GiB budget caps v1 at ~2–3 B-class INT4 models; 8B is a
  stretch goal needing heavier quant or streaming.
- **Next gate:** ADR remains provisional until the **ORT-GenAI native stack is
  observed loading + initialising inside the UWP container and emitting one token
  on the Xbox GPU** (full-path DLL load — bare-name is proven to fail). Throughput
  (tokens/s) is a follow-on measurement, not the gate.
