# core/CMakeLists.txt
# Builds xbai_core: the portable, platform-agnostic static library (ADR-0005).
# Plain C++20. ZERO WinRT/GDK includes -- enforced by review (ADR-0006 SS6).

cmake_minimum_required(VERSION 3.21)
project(xbai_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# OGA (ORT-GenAI) availability gate.
# OFF (default): compile OgaInferenceEngine against the local stub header;
#   no link dependency on onnxruntime-genai.lib.  Correct for the dev PC
#   which has no GPU and no OGA DLL installed.
# ON: compile against the real ort_genai_c.h and link onnxruntime-genai.lib.
#   Set this in UWP/GDK-xbox backends where the NuGet package is present.
#   XBAI_OGA_INCLUDE_DIR and XBAI_OGA_LIB must also be set when ON.
# ---------------------------------------------------------------------------
option(XBAI_HAVE_OGA "Compile and link against the real ORT-GenAI C API" OFF)

add_library(xbai_core STATIC
    core.cpp
    model/ModelLocator.cpp
    model/GenaiConfig.cpp
    inference/OgaInferenceEngine.cpp
)

# Consumers get core/ on their include path, so #include "core.h" and
# #include "pal/ISocketListener.h" resolve. pal/ holds interface headers only.
target_include_directories(xbai_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# The PAL contract headers are part of the lib's interface surface even though
# they compile no code (header-only abstract interfaces). Listed so IDEs/tools
# see them as belonging to the target.
target_sources(xbai_core PRIVATE
    core.h
    pal/types.h
    pal/IInferenceEngine.h
    pal/ISocketListener.h
    pal/ILifecycle.h
    pal/IModelStore.h
    pal/IDownloader.h
    pal/IInput.h
    pal/IDashboard.h
    model/ModelLocator.h
    model/GenaiConfig.h
    inference/OgaInferenceEngine.h
    inference/oga_stub.h
)

if (XBAI_HAVE_OGA)
    # Real OGA SDK: caller must supply the include dir (contains ort_genai_c.h)
    # and the import lib path via CMake cache variables.
    if (NOT DEFINED XBAI_OGA_INCLUDE_DIR OR NOT DEFINED XBAI_OGA_LIB)
        message(FATAL_ERROR
            "XBAI_HAVE_OGA=ON requires XBAI_OGA_INCLUDE_DIR and XBAI_OGA_LIB "
            "to be set.  Example:\n"
            "  -DXBAI_OGA_INCLUDE_DIR=<nupkg>/build/native/include\n"
            "  -DXBAI_OGA_LIB=<nupkg>/runtimes/win-x64/native/onnxruntime-genai.lib")
    endif()
    target_compile_definitions(xbai_core PRIVATE XBAI_HAVE_OGA)
    target_include_directories(xbai_core PRIVATE "${XBAI_OGA_INCLUDE_DIR}")
    target_link_libraries(xbai_core PRIVATE "${XBAI_OGA_LIB}")
endif()

if (MSVC)
    target_compile_options(xbai_core PRIVATE /W4 /permissive-)
endif()
