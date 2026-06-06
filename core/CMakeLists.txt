# core/CMakeLists.txt
# Builds xbai_core: the portable, platform-agnostic static library (ADR-0005).
# Plain C++20. ZERO WinRT/GDK includes — enforced by review (ADR-0006 §6).

cmake_minimum_required(VERSION 3.21)
project(xbai_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(xbai_core STATIC
    core.cpp
    model/ModelLocator.cpp
    model/GenaiConfig.cpp
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
)

if (MSVC)
    target_compile_options(xbai_core PRIVATE /W4 /permissive-)
endif()
