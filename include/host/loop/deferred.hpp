#pragma once

#include "host/loop/event_loop.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <vector>

namespace rwin::host {

// Things to do a little later, each at its own time, on one timer: asks of
// the GPU device that found nothing yet and are made again a moment on.
// Whatever has not been done when this goes is not done.
class Deferred {
public:
    explicit Deferred(EventLoop& loop);

    void after(std::chrono::milliseconds delay, std::function<void()> action);
    void clear() noexcept;

private:
    using Clock = std::chrono::steady_clock;
    struct Pending {
        Clock::time_point due;
        std::function<void()> action;
    };

    void fire();
    void arm();

    std::vector<Pending> pending_;
    std::vector<Pending> firing_;
    std::unique_ptr<EventLoop::Timer> timer_;
};

}  // namespace rwin::host
