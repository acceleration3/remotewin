#pragma once

#include <string>

struct sd_bus_error;
struct sd_bus_message;
struct sd_bus_slot;

namespace rwin::host {

class BusConnection;
class MirrorSession;

// What this mirror holds, for a person or a tool comparing it with the
// guest and the compositor: every window the guest has told of, in its
// z-order, whether it is the application's, and each mirror's own idea of
// the window beside it - its role, its parent, the state the compositor
// gave it. On the session bus at /org/remotewin/Windows of this process's
// own connection; tools/window_states.py reads it from every mirror
// running.
class WindowReport {
public:
    WindowReport(BusConnection& bus, const MirrorSession& session);
    ~WindowReport();

    WindowReport(const WindowReport&) = delete;
    WindowReport& operator=(const WindowReport&) = delete;
    WindowReport(WindowReport&&) = delete;
    WindowReport& operator=(WindowReport&&) = delete;

    // The report, as JSON.
    [[nodiscard]] std::string describe() const;

private:
    static int described(sd_bus_message* call, void* self, sd_bus_error* error);

    const MirrorSession& session_;
    sd_bus_slot* object_ = nullptr;
};

}  // namespace rwin::host
