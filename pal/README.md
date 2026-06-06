# pal/ — one thin backend per platform

Each subdirectory implements the `core/pal/` interfaces for one platform and provides that
platform's entry point. Core depends only on the interfaces; each shell's `main` wires in
the concrete implementations and links the `core/` static lib.

**Duplication between backends is fine** (ADR-0006 §3): no premature `pal/common`, no
shared base classes up front. Extract a shared seam later, from evidence.

**Build per ADR-0005:** each backend uses its native build system — there is no global one.

| Backend | Build | Platform APIs | Status |
|---|---|---|---|
| `gdk-desktop/` | CMake or MSBuild (`Gaming.Desktop.x64`) | WinMain + XGameRuntime, Winsock, WinHTTP/XCurl, Win32 paths, GameInput | **wave 1 — this dev PC, free** |
| `uwp/` | MSBuild (appx, C++/WinRT + XAML) | IFrameworkView, StreamSocketListener, BackgroundDownloader, ApplicationData, Windows.Gaming.Input | later wave (the console v1 ship) |
| `gdk-xbox/` | MSBuild (`Gaming.Xbox.Scarlett.x64`) | copies `gdk-desktop` + Xbox Graphics/Media/Storage extensions | gated flip (ID@Xbox/GDKX) — deferred |
