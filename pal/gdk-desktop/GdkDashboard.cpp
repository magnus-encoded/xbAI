// pal/gdk-desktop/GdkDashboard.cpp
#include "GdkDashboard.h"

#include <cstdio>

namespace xbai {

// Map the enum to a human-readable tag.
static const char* state_str(ServerState s) {
    switch (s) {
        case ServerState::Starting:     return "starting";
        case ServerState::Downloading:  return "downloading";
        case ServerState::LoadingModel: return "loading_model";
        case ServerState::Ready:        return "ready";
        case ServerState::Error:        return "error";
        default:                        return "unknown";
    }
}

void GdkDashboard::set_status(const DashboardStatus& s) {
    std::printf("[xbai] state=%s endpoint=%s tokens/s=%.2f queue=%d",
                state_str(s.state),
                s.endpoint_url.c_str(),
                s.tokens_per_second,
                s.queue_depth);
    if (!s.message.empty()) {
        std::printf(" msg=%s", s.message.c_str());
    }
    std::printf("\n");
    std::fflush(stdout);
}

}  // namespace xbai
