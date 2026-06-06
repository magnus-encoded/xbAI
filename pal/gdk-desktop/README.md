# pal/gdk-desktop/

GameCore **Win32** backend on the public PC GDK (`Gaming.Desktop.x64`, free, no signup).
Runs on the dev PC — the wave-1 harness. Implements the `core/pal/` seams with WinMain +
`XGameRuntime`, Winsock, WinHTTP/XCurl, Win32 paths, GameInput. The AppContainer tax
(bundled CRT, loopback ban) does not apply here.

Bootstrapped by **P0** (entry + Winsock skeleton); seams filled by the `pal-gdkdesktop-*`
tasks. See memory `dev-pc-gdk-desktop-harness` for what this box can/can't validate.
