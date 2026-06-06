// core/core.h
// Public surface of the xbai_core static lib. Plain C++ only — ZERO WinRT/GDK.
#pragma once

namespace xbai {

// Build/ABI sentinel for the core static lib. Bumped when the frozen PAL
// contract changes (see core/pal/*). Lets a shell assert it linked the core it
// expects. Returns 1 for the P0 walking skeleton.
int core_version();

}  // namespace xbai
