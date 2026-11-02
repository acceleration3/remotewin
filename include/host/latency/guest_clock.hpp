#pragma once

#include "host/latency/clock_offset.hpp"
#include "host/loop/event_loop.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace rwin {
struct ClockEcho;
}

namespace rwin::host {

class GuestLink;

class GuestClock {
public:
    GuestClock(EventLoop& loop, GuestLink& link);

    [[nodiscard]] std::optional<std::int64_t> to_host(std::int64_t guest) const noexcept {
        return offset_.to_host(guest);
    }

private:
    void heard(const ClockEcho& echo, std::int64_t when);

    GuestLink& link_;
    std::unique_ptr<EventLoop::Timer> timer_;
    ClockOffset offset_;
    std::size_t answers_ = 0;
};

}
