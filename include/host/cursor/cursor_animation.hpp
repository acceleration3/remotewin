#pragma once

#include "host/loop/event_loop.hpp"
#include "host/loop/signal.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace rwin::host {
class CursorAnimation {
public:
    explicit CursorAnimation(EventLoop& loop);

    void play(std::vector<int> delays_ms);

    [[nodiscard]] std::size_t step() const noexcept { return step_; }
    [[nodiscard]] std::uint64_t steps_taken() const noexcept { return taken_; }

    Signal<std::size_t> stepped;

private:
    void advance();

    static constexpr int kShortestMs = 16;

    std::unique_ptr<EventLoop::Timer> timer_;
    std::vector<int> delays_;
    std::size_t step_ = 0;
    std::uint64_t taken_ = 0;
};

}
