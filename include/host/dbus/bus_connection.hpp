#pragma once

#include "host/loop/event_loop.hpp"

#include <memory>
#include <string>

struct sd_bus;

namespace rwin::host {

// This process's connection to the session bus (sd-bus), read and written
// by the loop: what comes is processed as it comes, what is queued goes out
// as the socket takes it, and sd-bus's own timeouts are kept. Not connected
// where there is no session bus; whatever needs one then does without.
class BusConnection {
public:
    explicit BusConnection(EventLoop& loop);
    ~BusConnection();

    BusConnection(const BusConnection&) = delete;
    BusConnection& operator=(const BusConnection&) = delete;
    BusConnection(BusConnection&&) = delete;
    BusConnection& operator=(BusConnection&&) = delete;

    [[nodiscard]] bool connected() const noexcept { return bus_ != nullptr; }
    [[nodiscard]] sd_bus* bus() const noexcept { return bus_; }
    // ":1.234": what a script inside KWin calls this process by.
    [[nodiscard]] std::string unique_name() const;

    // Calls were queued: they go out, and the watch follows what sd-bus
    // waits for now.
    void pump();

private:
    void rearm();

    sd_bus* bus_ = nullptr;
    std::unique_ptr<EventLoop::Watch> watch_;
    std::unique_ptr<EventLoop::Timer> timeout_;
    bool processing_ = false;
};

}  // namespace rwin::host
