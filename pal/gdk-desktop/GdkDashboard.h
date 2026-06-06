// pal/gdk-desktop/GdkDashboard.h
// IDashboard implementation for the gdk-desktop / dev-PC harness.
// No on-screen UI surface on the dev PC; status is printed to stdout so it is
// visible in the console running the exe.
#pragma once

#include "pal/IDashboard.h"

namespace xbai {

class GdkDashboard : public IDashboard {
public:
    GdkDashboard() = default;
    ~GdkDashboard() override = default;

    // Print a status snapshot to stdout.
    void set_status(const DashboardStatus& status) override;
};

}  // namespace xbai
