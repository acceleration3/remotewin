#pragma once

#include <string>

struct sd_bus_error;
struct sd_bus_message;
struct sd_bus_slot;

namespace rwin::host {

class BusConnection;
class MirrorSession;

class WindowReport {
public:
    WindowReport(BusConnection& bus, const MirrorSession& session);
    ~WindowReport();

    WindowReport(const WindowReport&) = delete;
    WindowReport& operator=(const WindowReport&) = delete;
    WindowReport(WindowReport&&) = delete;
    WindowReport& operator=(WindowReport&&) = delete;

    [[nodiscard]] std::string describe() const;

private:
    static int described(sd_bus_message* call, void* self, sd_bus_error* error);

    const MirrorSession& session_;
    sd_bus_slot* object_ = nullptr;
};

}
