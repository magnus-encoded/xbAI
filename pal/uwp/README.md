# pal/uwp/

C++/WinRT + XAML backend for the retail console in UWP dev mode — the eventual **v1 ship**
(ADR-0003). MSBuild/appx (reuses `build.ps1` + signing). Implements the `core/pal/` seams
with IFrameworkView, StreamSocketListener, BackgroundDownloader, ApplicationData,
Windows.Gaming.Input — all already de-risked by the `xbprobe` probes (T2/T3/T4).

**Later wave** — current scope is gdk-desktop only. Per ADR-0006 §3, implemented
independently of gdk-desktop; duplication is fine.
