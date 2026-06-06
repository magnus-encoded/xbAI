// core/pal/IDashboard.h
// PAL seam: present status to the user (the on-screen dashboard). The platform
// owns the rendering surface; core pushes a status snapshot whenever it changes.
// Plain C++ only.
//
// === FROZEN by P0 (2026-06-06) === see types.h for the freeze rule.

#pragma once

#include <cstdint>
#include <string>

namespace xbai {

// High-level app state shown on the dashboard.
enum class ServerState {
    Starting,
    Downloading,  // fetching model weights
    LoadingModel,
    Ready,        // serving
    Error,
};

// A snapshot of what the dashboard should display. core builds and pushes this.
struct DashboardStatus {
    ServerState state = ServerState::Starting;
    std::string endpoint_url;   // "http://192.168.1.50:8080"
    double tokens_per_second = 0.0;
    int queue_depth = 0;        // requests waiting (single-flight: 0 or 1+)
    std::string message;        // free-form detail / error text
};

// Renders the current status. set_status may be called from any core thread;
// implementations marshal to their UI thread as needed.
class IDashboard {
public:
    virtual ~IDashboard() = default;

    virtual void set_status(const DashboardStatus& status) = 0;
};

}  // namespace xbai
