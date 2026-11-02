#pragma once

#include "host/loop/event_loop.hpp"

#include <memory>
#include <string>

struct sd_bus;

namespace rwin::host {
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
    [[nodiscard]] std::string unique_name() const;

    void pump();

private:
    void rearm();

    sd_bus* bus_ = nullptr;
    std::unique_ptr<EventLoop::Watch> watch_;
    std::unique_ptr<EventLoop::Timer> timeout_;
    bool processing_ = false;
};

}
