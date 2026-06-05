# ADR-0003: v1 is a developer tool with build-time model config, not the consumer one-button app

- Status: **Accepted**
- Date: 2026-06-04

## Context
`idea.md` pitches a consumer end-state: any Xbox owner installs a Store app, is
offered a Hugging Face model, presses **A**, and it downloads-on-device → serves →
dashboards. That flow assumes a vetted model catalog, an on-console download UX,
and naive-user packaging — none of which we need to *prove the core thesis* (that a
retail Xbox in dev mode can serve an open-weight model). It also collides with a
hard constraint: ORT-GenAI can only run models already exported to the
**ORT-GenAI ONNX/DirectML** format, so "any HF model" is not actually runnable
without an on-device conversion pipeline that won't fit the ~4 GiB budget anyway.

## Decision
Scope v1 as a **developer tool for experienced users on dev-enabled units**, while
keeping the real on-console flow:
- **Which** model is selected at **build/config time** — a text/JSON config names
  the HF repo + file set; the dev compiles with it and pushes to their dev unit.
- The **on-console UX is still the real UX**: the UI shows **a button for that one
  configured model**, the user presses **A** = "yes, download and run this", and it
  **downloads on-device → loads → serves → dashboards**. (Dev-time SMB staging to
  `LocalState` remains available as a probe/dev shortcut.)
- "Arbitrary model" means the **dev** may point the config at any model **that is
  already an ORT-GenAI ONNX/DirectML export** — the dev owns ensuring compatibility;
  we do not convert PyTorch/safetensors on-device.
- **Explicit no-s for v1:** no *choosing among* models (no in-app catalog, no
  on-console browsing), no other model-customization UX, no naive-user / MS-Store
  packaging.

The **multi-model selection layer** and consumer packaging from `idea.md` are the
**mature end-state**, deferred until v1 actually runs.

## Consequences
- The download-on-device path **is built in v1** (for the single configured model),
  writing to `LocalState/models/<id>/` — so the hard part of the consumer flow is
  exercised early, not deferred.
- The dashboard has a **single-model button**, not a select screen; after launch it
  shows status, endpoint URL, and live usage.
- Reaching the consumer end-state later is **additive** (a selection layer +
  packaging on top of a working single-model server), not a rewrite.
