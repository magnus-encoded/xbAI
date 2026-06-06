# Top-level CMake for the xbAI product build.
# core/ = portable static lib (ADR-0005). Per-backend shells live under pal/ and
# each select their own toolchain; the dev-PC harness (gdk-desktop) builds here.
# The UWP and Xbox-GDK shells use MSBuild/GDK props, not this CMake tree.

cmake_minimum_required(VERSION 3.21)
project(xbAI LANGUAGES CXX)

add_subdirectory(core)

# Build the dev-PC harness by default; it is the wave-1 target on this box.
add_subdirectory(pal/gdk-desktop)
