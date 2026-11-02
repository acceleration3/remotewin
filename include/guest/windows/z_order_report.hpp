#pragma once

#include "proto/z_order.hpp"

#include <chrono>
#include <optional>

namespace rwin::guest {

class MarkerWindow;
class Outbox;
class WindowTracker;

class ZOrderReport {
public:
    using Clock = std::chrono::steady_clock;

    ZOrderReport(const WindowTracker& tracker, const MarkerWindow& marker, Outbox& outbox) noexcept
        : tracker_(tracker), marker_(marker), outbox_(outbox) {}

    void expect(void* window) noexcept;

    void look(Clock::time_point now);

    [[nodiscard]] std::optional<Clock::time_point> due() const noexcept { return due_; }

private:
    static constexpr int kFirstWaitMs = 10;
    static constexpr int kLastWaitMs = 320;

    struct Stacking {
        ZOrder order;
        void* top = nullptr;
        void* top_ordinary = nullptr;
        void* active = nullptr;
    };

    [[nodiscard]] Stacking current() const;
    void send(const ZOrder& order);

    const WindowTracker& tracker_;
    const MarkerWindow& marker_;
    Outbox& outbox_;
    void* expected_ = nullptr;
    int wait_ms_ = 0;
    std::optional<Clock::time_point> due_;
    std::optional<ZOrder> sent_;
};

}
