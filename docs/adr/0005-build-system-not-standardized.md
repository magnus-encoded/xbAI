# ADR-0005: Build system deliberately not standardized (the null-hypothesis decision)

- Status: **Accepted**
- Date: 2026-06-06

## Context
The `core/` + `pal/` split (ADR-0004) raised an apparent fork: standardize the whole
product on **MSBuild** or on **CMake**? The instinct was to treat this as a weighty
decision. On inspection it is not. The capabilities are **mirrored everywhere except
one cell**:

| | MSBuild | CMake |
|---|---|---|
| `core/` (plain C++ lib) | fine | nicer — portable, CI-friendly, builds anywhere |
| `pal/gdk-desktop` / `gdk-xbox` (`Gaming.*.x64`) | first-class (GDK ships `.props`/`.targets` per platform) | supported (documented flat-deploy + toolchain files) |
| **`pal/uwp` (C++/WinRT + XAML)** | **first-class** (XAML compiler, appx packaging, WinRT projection all MSBuild-native) | **poor** — XAML compilation isn't really supported; fights the toolchain |

The only non-arbitrary constraint is that **XAML pins `pal/uwp` to MSBuild**, and that
asymmetry is *localized to one shell* — it does not force a global choice. Everything
else is mirrored, i.e. preference. This is also an **experimental project** where
re-doing an approach is par for the course, so the "don't have to start over" argument
for picking one carries little weight.

## Decision
**Do not standardize the build system.** Deciding not to decide is itself a decision,
recorded here so it is not re-litigated:

1. **`core/` is a consumable static library.** The integration contract is the **lib +
   its public headers**, *not* a shared build system. Any front-end links it.
2. **Each backend uses its native build:**
   - `pal/uwp` → **MSBuild** (forced by XAML / appx / C++/WinRT; reuses the existing
     `build.ps1` MSBuild-picking + signing machinery).
   - `core/` and the **GDK shells** → whatever is convenient. **CMake is encouraged for
     `core/`** (portable, testable on this GPU-less dev box and on CI, independent of any
     shell).
3. **Building `core/` under *both* MSBuild and CMake is acceptable and expected** — the
   lib boundary makes that free, not a problem to solve.

## Consequences
- No global build dependency couples the backends; the **lib boundary is the only
  integration contract**, which reinforces ADR-0004's portability goal.
- Two build idioms coexist in the tree. Accepted: the cost is small and the seams are
  clear (one build system per directory).
- **Revisit only if** the `core/`-as-a-lib boundary proves insufficient (e.g. a shell
  needs to compile core sources directly with shell-specific flags) — that, not build-tool
  preference, would be the real signal.
