# pal/gdk-desktop/CMakeLists.txt
# GameCore Win32 (Gaming.Desktop.x64) backend / dev-PC harness. Links the
# portable xbai_core static lib and implements the PAL seams over Winsock /
# Win32. Builds as a plain console exe when the public GDK is absent (dev PC);
# flip XBAI_HAVE_GDK + the Gaming.Desktop.x64 platform once GDK is installed.

cmake_minimum_required(VERSION 3.21)
project(xbai_gdk_desktop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(xbai_gdk_desktop
    main.cpp
    GdkSocketListener.cpp
    GdkModelStore.cpp
)

target_link_libraries(xbai_gdk_desktop PRIVATE xbai_core)

# getenv / Winsock inet_ntop etc. trip MSVC's "secure CRT" deprecations; the
# uses here are deliberate and bounded.
target_compile_definitions(xbai_gdk_desktop PRIVATE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)

# Detect the public GDK (Gaming.Desktop.x64). Absent on this dev box, so the
# build falls through to the plain-main() Winsock skeleton.
if (DEFINED ENV{GameDK} OR DEFINED ENV{GameDKLatest})
    message(STATUS "GDK detected -> enabling XBAI_HAVE_GDK (WinMain + XGameRuntime)")
    target_compile_definitions(xbai_gdk_desktop PRIVATE XBAI_HAVE_GDK)
    target_link_libraries(xbai_gdk_desktop PRIVATE xgameruntime.lib)
else()
    message(STATUS "GDK NOT detected -> building plain-main() Winsock skeleton")
endif()

if (MSVC)
    target_compile_options(xbai_gdk_desktop PRIVATE /W4 /permissive-)
endif()
