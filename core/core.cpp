// core/core.cpp
// P0 walking-skeleton placeholder. Proves the static lib builds and links into a
// shell. Real modules (inference/server/queue/model) land in later wave-1 tasks.
//
// Hard rule (ADR-0006 §6): nothing in core/ may #include a WinRT (winrt/...,
// Windows::) or GDK (XGame*, XCurl, GameInput) header.

#include "core.h"

namespace xbai {

int core_version() {
    return 1;
}

}  // namespace xbai
